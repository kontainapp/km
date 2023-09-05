/*
 * Copyright 2021-2022 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "km.h"

#include "km_coredump.h"
#include "km_filesys.h"
#include "km_fork.h"
#include "km_gdb.h"
#include "km_guest.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "km_signal.h"

int vcpu_dump = 0;
int km_collect_hc_stats = 0;
int kill_unimpl_hcall = 0;
// Prevent snapshotting until the payload is running.
int km_vcpus_are_started = 0;

#define fx "VCPU %d RIP 0x%0llx RSP 0x%0llx CR2 0x%llx "

#define __run_err(vcpu, __s, __f, ...)                                                             \
   km_err(__s, __f fx, ##__VA_ARGS__, vcpu->vcpu_id, vcpu->regs.rip, vcpu->regs.rsp, vcpu->sregs.cr2);

#define __run_errx(vcpu, __s, __f, ...)                                                            \
   km_errx(__s, __f fx, ##__VA_ARGS__, vcpu->vcpu_id, vcpu->regs.rip, vcpu->regs.rsp, vcpu->sregs.cr2);

#define run_err(__s, __f, ...) __run_err(vcpu, __s, __f, ##__VA_ARGS__)
#define run_errx(__s, __f, ...) __run_errx(vcpu, __s, __f, ##__VA_ARGS__)

#define __run_warn(vcpu, __f, ...)                                                                 \
   km_warnx(__f fx, ##__VA_ARGS__, vcpu->vcpu_id, vcpu->regs.rip, vcpu->regs.rsp, vcpu->sregs.cr2);

// only show this for verbose ('-V') runs
#define run_info(__f, ...)                                                                         \
   do {                                                                                            \
      if (km_trace_enabled() && (km_trace_enabled_tag() == 0 ||                                    \
                                 regexec(&km_info_trace.tags, KM_TRACE_KVM, 0, NULL, 0) == 0))     \
         __run_warn(__f, ##__VA_ARGS__);                                                           \
   } while (0)
#define run_infox(__f, ...)                                                                        \
   do {                                                                                            \
      if (km_trace_enabled() && (km_trace_enabled_tag() == 0 ||                                    \
                                 regexec(&km_info_trace.tags, KM_TRACE_KVM, 0, NULL, 0) == 0))     \
         __run_warn(vcpu, __f, ##__VA_ARGS__);                                                     \
   } while (0)

static inline void run_warn(const char* fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   km_warnx(fmt, args);
}

static const char* kvm_reason_name(int reason)
{
#define __KVM_REASON_MAX (KVM_EXIT_HYPERV + 1)   // this is the current max
#define __KVM_REASON_NAME(__r) [__r] = #__r
   static const_string_t reasons[__KVM_REASON_MAX] = {
       __KVM_REASON_NAME(KVM_EXIT_UNKNOWN),      __KVM_REASON_NAME(KVM_EXIT_EXCEPTION),
       __KVM_REASON_NAME(KVM_EXIT_IO),           __KVM_REASON_NAME(KVM_EXIT_HYPERCALL),
       __KVM_REASON_NAME(KVM_EXIT_DEBUG),        __KVM_REASON_NAME(KVM_EXIT_HLT),
       __KVM_REASON_NAME(KVM_EXIT_MMIO),         __KVM_REASON_NAME(KVM_EXIT_IRQ_WINDOW_OPEN),
       __KVM_REASON_NAME(KVM_EXIT_SHUTDOWN),     __KVM_REASON_NAME(KVM_EXIT_FAIL_ENTRY),
       __KVM_REASON_NAME(KVM_EXIT_INTR),         __KVM_REASON_NAME(KVM_EXIT_SET_TPR),
       __KVM_REASON_NAME(KVM_EXIT_TPR_ACCESS),   __KVM_REASON_NAME(KVM_EXIT_DCR), /* deprecated */
       __KVM_REASON_NAME(KVM_EXIT_NMI),          __KVM_REASON_NAME(KVM_EXIT_INTERNAL_ERROR),
       __KVM_REASON_NAME(KVM_EXIT_OSI),          __KVM_REASON_NAME(KVM_EXIT_PAPR_HCALL),
       __KVM_REASON_NAME(KVM_EXIT_WATCHDOG),     __KVM_REASON_NAME(KVM_EXIT_EPR),
       __KVM_REASON_NAME(KVM_EXIT_SYSTEM_EVENT), __KVM_REASON_NAME(KVM_EXIT_IOAPIC_EOI),
       __KVM_REASON_NAME(KVM_EXIT_HYPERV),
   };
#undef __KVM_REASON_NAME

   return reason <= __KVM_REASON_MAX ? reasons[reason] : "No such reason";
}

#define FMT_BUF(cur, rem, ...)                                                                     \
   {                                                                                               \
      int count = snprintf(cur, rem, __VA_ARGS__);                                                 \
      if (count > rem) {                                                                           \
         return NULL;                                                                              \
      }                                                                                            \
      cur += count;                                                                                \
      rem -= count;                                                                                \
      if (rem == 0) {                                                                              \
         return NULL;                                                                              \
      }                                                                                            \
   }
static char* dump_regs(km_vcpu_t* vcpu, char* buf, size_t len)
{
   char* cur = buf;
   size_t rem = len;

   km_read_registers(vcpu);
   FMT_BUF(cur, rem, "== KVM_GET_REGS ==\n");
   FMT_BUF(cur,
           rem,
           "RAX: 0x%-16llx RBX: 0x%-16llx RCX: 0x%-16llx RDX: 0x%-16llx\n",
           vcpu->regs.rax,
           vcpu->regs.rbx,
           vcpu->regs.rcx,
           vcpu->regs.rdx);
   FMT_BUF(cur,
           rem,
           "RSI: 0x%-16llx RDI: 0x%-16llx RSP: 0x%-16llx RBP: 0x%-16llx\n",
           vcpu->regs.rsi,
           vcpu->regs.rdi,
           vcpu->regs.rsp,
           vcpu->regs.rbp);
   FMT_BUF(cur,
           rem,
           "R8:  0x%-16llx R9:  0x%-16llx R10: 0x%-16llx R11: 0x%-16llx\n",
           vcpu->regs.r8,
           vcpu->regs.r9,
           vcpu->regs.r10,
           vcpu->regs.r11);
   FMT_BUF(cur,
           rem,
           "R12: 0x%-16llx R13: 0x%-16llx R14: 0x%-16llx R15: 0x%-16llx\n",
           vcpu->regs.r12,
           vcpu->regs.r13,
           vcpu->regs.r14,
           vcpu->regs.r15);

   FMT_BUF(cur, rem, "RIP: 0x%-16llx RFLAGS: 0x%llx\n", vcpu->regs.rip, vcpu->regs.rflags);

   unsigned char* instr = km_gva_to_kma(vcpu->regs.rip);
   if (instr != NULL) {
      FMT_BUF(cur,
              rem,
              "0x%-16llx: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
              vcpu->regs.rip,
              instr[0],
              instr[1],
              instr[2],
              instr[3],
              instr[4],
              instr[5],
              instr[6],
              instr[7]);
   }
   return cur;
}

static char* dump_segment_register(char* name, struct kvm_segment* seg, char* buf, size_t len)
{
   char* cur = buf;
   size_t rem = len;

   FMT_BUF(cur,
           rem,
           "%-3s base: 0x%-16llx limit:0x%x selector: 0x%x type: 0x%x present: 0x%x\n",
           name,
           seg->base,
           seg->limit,
           seg->selector,
           seg->type,
           seg->present);
   FMT_BUF(cur,
           rem,
           "    dpl:0x%x db:0x%x s:0x%x l:0x%x g:0x%x avl:0x%x unusable:0x%x\n",
           seg->dpl,
           seg->db,
           seg->s,
           seg->l,
           seg->g,
           seg->avl,
           seg->unusable);

   return cur;
}

#define FMT_SREG(name, regp, cur, rem)                                                             \
   {                                                                                               \
      int count;                                                                                   \
      char* ptr = dump_segment_register(name, regp, cur, rem);                                     \
      if (ptr == NULL) {                                                                           \
         return NULL;                                                                              \
      }                                                                                            \
      count = ptr - cur;                                                                           \
      if (count > rem) {                                                                           \
         return NULL;                                                                              \
      }                                                                                            \
      cur += count;                                                                                \
      rem -= count;                                                                                \
      if (rem == 0) {                                                                              \
         return NULL;                                                                              \
      }                                                                                            \
   }

static char* dump_sregs(km_vcpu_t* vcpu, char* buf, size_t len)
{
   char* cur = buf;
   size_t rem = len;

   km_read_sregisters(vcpu);
   FMT_BUF(cur, rem, "== KVM_GET_SREGS ==\n");
   FMT_SREG("CS", &vcpu->sregs.cs, cur, rem);
   FMT_SREG("DS", &vcpu->sregs.ds, cur, rem);
   FMT_SREG("ES", &vcpu->sregs.es, cur, rem);
   FMT_SREG("FS", &vcpu->sregs.fs, cur, rem);
   FMT_SREG("GS", &vcpu->sregs.gs, cur, rem);
   FMT_SREG("SS", &vcpu->sregs.ss, cur, rem);
   FMT_SREG("TR", &vcpu->sregs.tr, cur, rem);
   FMT_SREG("LDT", &vcpu->sregs.ldt, cur, rem);
   FMT_BUF(cur, rem, "GDT base: 0x%llx limit: 0x%x\n", vcpu->sregs.gdt.base, vcpu->sregs.gdt.limit);
   FMT_BUF(cur, rem, "IDT base: 0x%llx limit: 0x%x\n", vcpu->sregs.idt.base, vcpu->sregs.idt.limit);
   FMT_BUF(cur,
           rem,
           "CR0: 0x%llx CR2: 0x%llx CR3: 0x%llx CR4: 0x%llx CR8: 0x%llx\n",
           vcpu->sregs.cr0,
           vcpu->sregs.cr2,
           vcpu->sregs.cr3,
           vcpu->sregs.cr4,
           vcpu->sregs.cr8);
   FMT_BUF(cur, rem, "EFER: 0x%llx APIC_BASE: 0x%llxc\n", vcpu->sregs.efer, vcpu->sregs.apic_base);

   FMT_BUF(cur, rem, "INTERRUPT BITMAP:");

   for (int i = 0; i < sizeof(vcpu->sregs.interrupt_bitmap) / sizeof(__u64); i++) {
      FMT_BUF(cur, rem, " 0x%llx", vcpu->sregs.interrupt_bitmap[i]);
   }
   FMT_BUF(cur, rem, "\n");
   return cur;
}

static char* dump_events(km_vcpu_t* vcpu, char* buf, size_t len)
{
   struct kvm_vcpu_events events;
   char* cur = buf;
   size_t rem = len;

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_VCPU_EVENTS, &events) < 0) {
      fprintf(stderr, "KVM_GET_VCPU_EVENTS failed: %d - %s\n", errno, strerror(errno));
      return NULL;
   }
   FMT_BUF(cur, rem, "== EVENTS ==\n");
   FMT_BUF(cur,
           rem,
           "EXCEPTION: injected: 0x%x nr: 0x%x has_error_code: 0x%x error_code: 0x%x\n",
           events.exception.injected,
           events.exception.nr,
           events.exception.has_error_code,
           events.exception.error_code);
   FMT_BUF(cur,
           rem,
           "INTERRUPT: injected: 0x%x nr: 0x%x soft: 0x%x shadow: 0x%x\n",
           events.interrupt.injected,
           events.interrupt.nr,
           events.interrupt.soft,
           events.interrupt.shadow);
   FMT_BUF(cur,
           rem,
           "NMI:       injected: 0x%x nr: 0x%x masked: 0x%x pad: 0x%x\n",
           events.nmi.injected,
           events.nmi.pending,
           events.nmi.masked,
           events.nmi.pad);
   FMT_BUF(cur, rem, "SIPI_VECTOR: 0x%x FLAGS: 0x%x\n", events.sipi_vector, events.flags);
   FMT_BUF(cur,
           rem,
           "SMI:       smm: 0x%x pending: 0x%x inside_nmi: 0x%x latched_init: 0x%x\n",
           events.smi.smm,
           events.smi.pending,
           events.smi.smm_inside_nmi,
           events.smi.latched_init);
   return cur;
}

void km_dump_vcpu(km_vcpu_t* vcpu)
{
   char buf[4096];
   char* next;
   size_t len = sizeof(buf);

   next = dump_regs(vcpu, buf, len);
   if (next == NULL) {
      fprintf(stderr, "ERROR - NULL return from dump_regs\n");
      return;
   }
   len = sizeof(buf) - (next - buf);

   next = dump_sregs(vcpu, next, len);
   if (next == NULL) {
      fprintf(stderr, "ERROR - NULL return from dump_sregs\n");
      return;
   }
   len = sizeof(buf) - (next - buf);

   next = dump_events(vcpu, next, len);
   if (next == NULL) {
      km_warnx("ERROR - NULL return from dump_events");
      return;
   }

   km_warnx("%s", buf);
}

/*
 * populate vcpu->regs with register values
 */
void km_read_registers(km_vcpu_t* vcpu)
{
   if (vcpu->regs_valid != 0) {
      return;
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &vcpu->regs) < 0) {
      km_warn("KVM_GET_REGS failed");
      return;
   }
   vcpu->regs_valid = 1;
}

void km_write_registers(km_vcpu_t* vcpu)
{
   if (vcpu->regs_valid == 0) {
      km_errx(2, "registers not valid");
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_REGS, &vcpu->regs) < 0) {
      km_warn("KVM_SET_REGS failed");
      return;
   }
}

/*
 * populate vcpu->sregs with segment register values
 */
void km_read_sregisters(km_vcpu_t* vcpu)
{
   if (vcpu->sregs_valid != 0) {
      return;
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_SREGS, &vcpu->sregs) < 0) {
      km_warn("KVM_GET_SREGS failed");
      return;
   }
   vcpu->sregs_valid = 1;
}

void km_write_sregisters(km_vcpu_t* vcpu)
{
   if (vcpu->sregs_valid == 0) {
      km_errx(2, "sregisters not valid");
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0) {
      km_warn("KVM_SET_SREGS failed");
      return;
   }
}

void km_write_xcrs(km_vcpu_t* vcpu)
{
   kvm_xcrs_t xcrs;

   // XCR0 never changes. No need to save anywhere.
   xcrs.nr_xcrs = 1;
   xcrs.flags = 0;
   xcrs.xcrs[0].xcr = 0;
   xcrs.xcrs[0].reserved = 0;
   xcrs.xcrs[0].value = machine.xcr0;
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_XCRS, &xcrs) < 0) {
      km_warn("KVM_SET_XCRS failed");
      return;
   }
}

/*
 * return non-zero and set status if guest halted
 */
static int hypercall(km_vcpu_t* vcpu, int* hc_ret)
{
   kvm_run_t* r = vcpu->cpu_run;
   km_gva_t ga;
   uint16_t hc;

   if (vcpu->restart != 0) {
      hc = vcpu->hypercall;
      vcpu->restart = 0;
   } else {
      /* Sanity checks */
      hc = r->io.port - KM_HCALL_PORT_BASE;
      if (!(r->io.direction == KVM_EXIT_IO_OUT && r->io.size == 4 && hc >= 0 && hc < KM_MAX_HCALL)) {
         km_info(KM_TRACE_SIGNALS,
                 "KVM: unexpected IO port activity, port 0x%x 0x%x bytes %s",
                 r->io.port,
                 r->io.size,
                 r->io.direction == KVM_EXIT_IO_OUT ? "out" : "in");

         siginfo_t info = {.si_signo = SIGBUS, .si_code = SI_KERNEL};
         km_post_signal(vcpu, &info);
         return -1;
      }
      if (kill_unimpl_hcall != 0 && km_hcalls_table[hc] == NULL) {
         km_warnx("Unimplemented hypercall %d (%s)", hc, km_hc_name_get(hc));
         siginfo_t info = {.si_signo = SIGSYS, .si_code = SI_KERNEL};
         km_post_signal(vcpu, &info);
         return -1;
      }
      vcpu->hypercall = hc;
   }
   // We assume hypercall args are built are on stack in the guest, but nothing in km depends on this.
   ga = (km_gva_t)km_hcargs[HC_ARGS_INDEX(vcpu->vcpu_id)];
   km_hc_args_t* ga_kma = km_gva_to_kma(ga);
   km_infox(KM_TRACE_HC, "calling hc = %d (%s), hcargs gva 0x%lx, kma %p", hc, km_hc_name_get(hc), ga, ga_kma);
   if (ga_kma == NULL || km_gva_to_kma(ga + sizeof(km_hc_args_t) - 1) == NULL) {
      km_infox(KM_TRACE_SIGNALS, "hc: %d bad stack_top km_hc_args_t address:0x%lx", hc, ga);
      siginfo_t info = {.si_signo = SIGSEGV, .si_code = SI_KERNEL};
      km_post_signal(vcpu, &info);
      return -1;
   }
   struct timespec start, stop;
   if (km_collect_hc_stats != 0) {
      clock_gettime(CLOCK_MONOTONIC, &start);
   }
   km_hc_ret_t ret = HC_CONTINUE;
   if (km_hcalls_table[hc] != NULL) {
      ret = km_hcalls_table[hc](vcpu, hc, ga_kma);
      // check for interrupted hypercall, set restart if needed
      if (ga_kma->hc_ret == -EINTR && vcpu->state == HCALL_INT) {
         vcpu->regs_valid = 0;
         km_vcpu_sync_rip(vcpu);   // sync RIP with KVM
         vcpu->restart = 1;
      } else {
         vcpu->restart = 0;
      }
   } else {
      km_infox(KM_TRACE_HC, "Unimplemented hypercall %d (%s)", hc, km_hc_name_get(hc));
      ga_kma->hc_ret = (uint64_t)-ENOTSUP;
   }
   *hc_ret = ga_kma->hc_ret;
   if (km_collect_hc_stats != 0) {
      clock_gettime(CLOCK_MONOTONIC, &stop);
      uint64_t msecs = (stop.tv_sec - start.tv_sec) * 1000000000 + stop.tv_nsec - start.tv_nsec;
      km_infox(KM_TRACE_HC, "calling hc = %d (%s) time=%ld", hc, km_hc_name_get(hc), msecs);
      km_hc_stats_t* hstat = &km_hcalls_stats[hc];
      hstat->min = MIN(msecs, hstat->min);
      hstat->max = MAX(msecs, hstat->max);
      hstat->total += msecs;
      hstat->count++;
   }
   return ret;
}

static void km_vcpu_exit_all(km_vcpu_t* vcpu)
{
   int exiting_already;

   km_mutex_lock(&machine.vm_vcpu_mtx);
   exiting_already = machine.exit_group;
   machine.exit_group = 1;   // make sure we exit and not waiting for gdb
   km_mutex_unlock(&machine.vm_vcpu_mtx);
   if (exiting_already == 0) {
      km_vcpu_pause_all(vcpu, ALL);
      km_signal_machine_fini();
   }
   km_vcpu_stopped(vcpu);
}

/*
 * Call ioctl(KVM_RUN) once, and handles error return from ioctl.
 * Returns 0 on success -1 on ioctl error (an indication that normal exit_reason handling should be
 * skipped upstairs)
 */
static void km_vcpu_one_kvm_run(km_vcpu_t* vcpu)
{
   int rc;

   vcpu->state = IN_GUEST;
   vcpu->cpu_run->exit_reason = KVM_EXIT_UNKNOWN;   // Clear exit_reason from the preceding ioctl
   vcpu->regs_valid = 0;                            // Invalidate cached registers
   vcpu->sregs_valid = 0;
   km_infox(KM_TRACE_VCPU, "about to ioctl( KVM_RUN )");
   rc = ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL);
   vcpu->state = HYPERCALL;
   km_infox(KM_TRACE_VCPU,
            "ioctl( KVM_RUN ) returned %d KVM_RUN exit %d (%s)",
            rc,
            vcpu->cpu_run->exit_reason,
            kvm_reason_name(vcpu->cpu_run->exit_reason));

   if (km_trace_enabled() || km_gdb_client_is_attached()) {
      km_read_registers(vcpu);
      km_read_sregisters(vcpu);
   }

   if (rc != 0) {
      km_info(KM_TRACE_KVM,
              "RIP 0x%0llx RSP 0x%0llx CR2 0x%llx",
              vcpu->regs.rip,
              vcpu->regs.rsp,
              vcpu->sregs.cr2);
      vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
      switch (errno) {
         case EINTR:
            break;

         case EFAULT: {
            /*
             * This happens when the guest violates memory protection, for example writes to the
             * text area. This is a side-effect of how we protect guest memory (guest PT says page
             * is writable, but kernel says it isn't).
             */
            siginfo_t info = {.si_signo = SIGSEGV, .si_code = SI_KERNEL};
            info.si_addr = km_find_faulting_address(vcpu);
            km_post_signal(vcpu, &info);
            break;
         }
         default:
            run_err(1, "KVM: vcpu run failed");
      }
   }
}

/*
 * Return 1 if the passed vcpu has been running or stepping.
 * Return 0 if the passed vcpu has been paused.
 * Skip the vcpu skip_me.
 */
static int km_vcpu_is_running(km_vcpu_t* vcpu, void* skip_me)
{
   if (vcpu == skip_me) {
      return 0;   // Don't count this vcpu
   }
   return (vcpu->gdb_vcpu_state.gdb_run_state == THREADSTATE_PAUSED) ? 0 : 1;
}

static inline void km_vcpu_handle_pause(km_vcpu_t* vcpu, int hc_ret)
{
   if (vcpu->snap_state == SNAP_STATE_RUNHOOK_CREATE) {
      return;
   }
   if (vcpu->snap_state == SNAP_STATE_RUNHOOK_RESTORE) {
      return;
   }

   /*
    * Interlock with machine.pause_requested. This is mostly for the benefits of gdb stub, but
    * also used to stop all vcpus in exit_grp() and in fatal signal. .pause_requested makes all
    * vcpu threads pause in cv_wait below. In case of exit_grp() and fatal signal they never run
    * again as we are exiting.
    *
    * In case of gdb the stub will set .pause_requested to 0 and broadcast the cv. The threads then
    * re-evaluate if they can run. gdb stub conveys desired run state in the .gdb_run_state. It can
    * allow all threads to run (gdb continue); or pause all but one, which will be stepping (gdb next
    * or step). gdb stub changes these fields *only* when .pause_requested is set to 1, hence it is
    * safe to check them here, even though gdb stub doesn't keep the pause_mtx lock when changing them.
    */
   km_mutex_lock(&machine.pause_mtx);
   while ((machine.pause_requested != 0 || (km_gdb_client_is_attached() != 0 &&
                                            vcpu->gdb_vcpu_state.gdb_run_state == THREADSTATE_PAUSED) ||
           vcpu->snap_state == SNAP_STATE_PAUSED) && machine.exit_group == 0) {
      km_infox(KM_TRACE_VCPU,
               "pause_requested %d, gvs_gdb_run_state %d, vcpu state %d",
               machine.pause_requested,
               vcpu->gdb_vcpu_state.gdb_run_state,
               vcpu->state);
      vcpu->state = PAUSED;
      km_cond_wait(&machine.pause_cv, &machine.pause_mtx);
      vcpu->state = HYPERCALL;
   }
   km_mutex_unlock(&machine.pause_mtx);
   // if exit_group() or equivalent happened while we were sleeping, like destructive snapshot, handle it
   if (machine.exit_group != 0) {   // exit_group() - we are done.
      km_vcpu_stopped(vcpu);        // Clean up and exit the current VCPU thread
      km_abortx("Reached the unreachable");
   }
}

void* km_vcpu_run(km_vcpu_t* vcpu)
{
   int hc_ret = 0;

   char thread_name[16];   // see 'man pthread_getname_np'
   sprintf(thread_name, "vcpu-%d", vcpu->vcpu_id);
   km_setname_np(vcpu->vcpu_thread, thread_name);

   if (vcpu->snap_state == SNAP_STATE_RUNHOOK_RESTORE) {
      km_assert(machine.sigactions[km_sigindex(KM_SIGSNAPRESTORE)].handler != 0); 
      siginfo_t info = {
         .si_signo = KM_SIGSNAPRESTORE,
         .si_code = SI_KERNEL,
      };
      km_deliver_signal(vcpu, &info);
   }

   while (1) {
      int reason;

      km_vcpu_handle_pause(vcpu, hc_ret);
      if (vcpu->restart == 0) {
         km_vcpu_one_kvm_run(vcpu);
         reason = vcpu->cpu_run->exit_reason;   // just to save on code width down the road
      } else {
         reason = KVM_EXIT_IO;
      }
      km_info(KM_TRACE_KVM,
              "RIP 0x%0llx RSP 0x%0llx CR2 0x%llx KVM: exit reason=%d (%s)",
              vcpu->regs.rip,
              vcpu->regs.rsp,
              vcpu->sregs.cr2,
              reason,
              kvm_reason_name(reason));
      switch (reason) {
         case KVM_EXIT_INTR:   // handled in km_vcpu_one_kvm_run
            break;

         case KVM_EXIT_IO:
            switch (hypercall(vcpu, &hc_ret)) {
               case HC_CONTINUE:
                  km_infox(KM_TRACE_VCPU,
                           "return from hc = %d (%s), hc_ret 0x%x, gdb_run_state %d, "
                           "pause_requested %d",
                           vcpu->hypercall,
                           km_hc_name_get(vcpu->hypercall),
                           hc_ret,
                           vcpu->gdb_vcpu_state.gdb_run_state,
                           machine.pause_requested);
                  break;

               case HC_STOP:
                  /*
                   * This thread has executed pthread_exit() or SYS_exit and is terminating. If we
                   * running with gdb and there are no more running threads (either we are the only
                   * thread or gdb paused all the others) we need to let gdb know that this thread
                   * is gone so that it can notify the user (and in latter case give them a chance to
                   * get at least one of the other threads going).
                   * We need to wake gdb before calling km_vcpu_stopped() because km_vcpu_stopped()
                   * will block until a new thread is created and reuses this vcpu.
                   */
                  km_info(KM_TRACE_KVM,
                          "RIP 0x%0llx RSP 0x%0llx CR2 0x%llx KVM: hypercall %d stop",
                          vcpu->regs.rip,
                          vcpu->regs.rsp,
                          vcpu->sregs.cr2,
                          vcpu->hypercall);
                  if (km_gdb_client_is_attached() != 0 &&
                      km_vcpu_apply_all(km_vcpu_is_running, vcpu) == 0) {
                     /*
                      * We notify gdb but don't wait because we need to go on and park the vcpu
                      * by calling km_vcpu_stopped().
                      */
                     vcpu->cpu_run->exit_reason = KVM_EXIT_DEBUG;
                     km_gdb_notify(vcpu, GDB_KMSIGNAL_THREADEXIT);
                  }
                  km_vcpu_stopped(vcpu);
                  break;

               case HC_ALLSTOP:
                  // This thread has executed exit_group() and the payload is terminating.
                  km_infox(KM_TRACE_KVM,
                           "RIP 0x%0llx RSP 0x%0llx CR2 0x%llx KVM: hypercall %d allstop, status "
                           "0x%x",
                           vcpu->regs.rip,
                           vcpu->regs.rsp,
                           vcpu->sregs.cr2,
                           vcpu->hypercall,
                           machine.exit_status);
                  km_gdb_accept_stop();
                  km_vcpu_exit_all(vcpu);
                  break;

               case HC_DOFORK:
                  // Give control to the km main thread which might be waiting for process
                  // exit or could be sleeping in gdbstub.  The fork() or clone() is done in km main.
                  km_infox(KM_TRACE_FORK,
                           "xfer control to km main thread, gdb enabled %d",
                           gdbstub.enabled);
                  if (km_gdb_is_enabled() != 0) {
                     km_gdb_notify(vcpu, GDB_KMSIGNAL_DOFORK);
                  } else {
                     km_vcpu_pause_all(vcpu, GUEST_ONLY);
                     if (eventfd_write(machine.shutdown_fd, 1) == -1) {
                        km_errx(2, "Failed to send machine_fini signal");
                     }
                  }
                  // Now go block in km_vcpu_handle_pause()
                  break;
            }
            break;   // exit_reason, case KVM_EXIT_IO

         case KVM_EXIT_DEBUG:
            if (km_gdb_client_is_attached() != 0) {
               /*
                * We handle stepping through a range of addresses here.
                * If we are in the address stepping range, we just turn around and keep
                * stepping.  Once we exit the address range we give control to the
                * gdb server thread.
                * If we hit a breakpoint planted in the stepping range we exit to gdb.
                */
               if (vcpu->gdb_vcpu_state.gdb_run_state == THREADSTATE_RANGESTEPPING &&
                   vcpu->cpu_run->debug.arch.pc >= vcpu->gdb_vcpu_state.steprange_start &&
                   vcpu->cpu_run->debug.arch.pc < vcpu->gdb_vcpu_state.steprange_end &&
                   (vcpu->cpu_run->debug.arch.dr6 & 0x4000) != 0) {
                  continue;
               }
               /*
                * To find out which breakpoint fired we need to look into this processors kvm_run
                * structure. In particular the kvm_debug_exit_arch structure. We use the pseudo
                * signal GDB_KMSIGNAL_KVMEXIT to cause the gdb payload handler to look into the
                * vcpu's kvm_run structure to figure out what has happened so that it can generate
                * the correct gdb stop reply.
                * If the signal deliver code below finds a signal to deliver it will try to
                * use this vcpu's gdb event which we have just taken here by calling km_gdb_notify().
                */
               km_gdb_notify(vcpu, GDB_KMSIGNAL_KVMEXIT);
            } else {
               /*
                * We got a KVM_EXIT_DEBUG but gdb is disabled.
                * This can happen if the gdb client disconnects the connection to the gdb server
                * because the gdb server sent something unexpected.  We should try to continue on
                * here. But, we also need to understand what the gdb server did to upset the gdb
                * client and fix that too.
                */
               run_warn("KVM: vcpu-%d debug exit while gdb is disabled, gdb_run_state %d, "
                        "pause_requested %d",
                        vcpu->vcpu_id,
                        vcpu->gdb_vcpu_state.gdb_run_state,
                        machine.pause_requested);
            }
            break;

         case KVM_EXIT_UNKNOWN: {
            km_read_registers(vcpu);
            unsigned char* ins = km_gva_to_kma(vcpu->regs.rip);
            /*
             * 0xf4 == HLT. Treat halt as SIGSEGV. For KKM.
             */
            if (ins != NULL && *ins == 0xf4) {
               siginfo_t info = {.si_signo = SIGSEGV,
                                 .si_code = SI_KERNEL,
                                 .si_addr = (void*)vcpu->regs.rip};
               km_post_signal(vcpu, &info);

            } else {
               run_errx(1, "KVM: unknown err 0x%llx", vcpu->cpu_run->hw.hardware_exit_reason);
            }
            break;
         }

         case KVM_EXIT_FAIL_ENTRY:
            run_warn("KVM_EXIT_FAIL_ENTRY");
            km_dump_vcpu(vcpu);
            run_errx(1, "KVM: fail entry 0x%llx", vcpu->cpu_run->fail_entry.hardware_entry_failure_reason);
            break;

         case KVM_EXIT_INTERNAL_ERROR:
            run_errx(1, "KVM: internal error, suberr 0x%x", vcpu->cpu_run->internal.suberror);
            break;

         case KVM_EXIT_SHUTDOWN:
            run_warn("KVM: shutdown");
            if (vcpu_dump) {
               km_dump_vcpu(vcpu);
            }
            abort();
            break;

         case KVM_EXIT_HLT: {
            /*
             * Guest executed a HLT instruction
             */
            km_read_registers(vcpu);
            siginfo_t info = {.si_signo = SIGSEGV,
                              .si_code = SI_KERNEL,
                              .si_addr = (void*)vcpu->regs.rip};
            km_post_signal(vcpu, &info);
            break;
         }

         case KVM_EXIT_EXCEPTION:
         default:
            run_errx(1, "KVM: exit. reason=%d (%s)", reason, kvm_reason_name(reason));
            break;
      }   // switch(reason)
      if (vcpu->hypercall_returns_signal != 0) {
         // rt_sigtimedwait() returns signals directly.  Let's not pile another signal on this
         // thread this time thru.
         if (km_gdb_client_is_attached() != 0) {
            km_gdb_notify(vcpu, vcpu->hypercall_returns_signal);
            // When gdbstub delivers the signal from gdb client, it will need the
            // hypercall_returns_signal flag so we don't clear it here.
         } else {
            // gdb is not interfering with signal delivery
            vcpu->hypercall_returns_signal = 0;
         }
      } else {
         siginfo_t info;
         if (km_dequeue_signal(vcpu, &info) != 0) {
            if (km_gdb_client_is_attached() != 0) {
               km_gdb_notify(vcpu, info.si_signo);
               /*
                * The gdb client should send the signal back and gdb stub will call
                * km_deliver_signal(). But, will the gdb client send the same signal back?  Or will
                * the client eat the signal? If we got here from sigsuspend() and the gdb client ate
                * the signal or sent a different signal back, we should really go back to wait for
                * an unblocked signal in sigsuspend().
                */
            } else {
               km_deliver_signal(vcpu, &info);
            }
            km_rt_sigsuspend_revert(vcpu);
         }
      }
   }
}

/*
 * Signal handler. Used when we want VCPU to stop. For vcpu->state == IN_GUEST the signal causes
 * KVM_RUN exit with -EINTR. It also interrupts some system calls, which we mark with HCALL_INT.
 *
 * We use this for two reasons - to stop vcpus for snapshot, exit or gdb; or to notify there is a
 * signal pending. We distinguish these two cases by setting VCPU_STOP bit in the si_int field.
 *
 * Calling km_info() and km_infox() from this function seems to occasionally cause a mutex
 * deadlock in the regular expression code called from km_info*().
 */
static void km_vcpu_pause_sighandler(int signum, siginfo_t* info, void* ucontext_unused)
{
   if ((info->si_int & VCPU_STOP) != 0) {
      km_vcpu_t* vcpu = machine.vm_vcpus[info->si_int & ~VCPU_STOP];
      if (vcpu->state == HYPERCALL) {
         vcpu->state = HCALL_INT;
      }
   }
}

/*
 * Forward a signal file descriptor received by KM into the guest signal system.
 * This relies on info->si_fd being populated.
 */
static void km_forward_sigio(int signo, siginfo_t* sinfo, void* ucontext_unused)
{
   int guest_fd = km_fs_h2g_fd(sinfo->si_fd);
   if (guest_fd < 0) {
      return;
   }
   siginfo_t info = *sinfo;
   info.si_fd = guest_fd;
   km_post_signal(NULL, &info);
}

/*
 * This is the signal handler for some of the signals sent to km but really are for the payload.
 * We then propagate these signals on to the vcpu threads. We need to be careful to not take
 * mutexes that could be held by the thread this signal handler is running on.  The intent is
 * that signal handler only runs on km's main thread which should only be running gdbstub. So, we
 * only take mutexes that the km main thread will not be holding.
 */
void km_signal_passthru(int signo, siginfo_t* sinfo, void* ucontext)
{
   siginfo_t info = *sinfo;
   km_post_signal(NULL, &info);
}

static int km_start_single_vcpu(km_vcpu_t* vcpu, void* unused)
{
   return km_run_vcpu_thread(vcpu);
}

void km_start_all_vcpus(void)
{
   if (km_vcpu_apply_all(km_start_single_vcpu, NULL) != 0) {
      km_err(2, "Failed to start guest");
   }
}

void km_start_vcpus()
{
   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpu_pause_sighandler);
   km_install_sighandler(SIGPIPE, km_signal_passthru);
   km_install_sighandler(SIGIO, km_forward_sigio);
   km_install_sighandler(SIGINT, km_signal_passthru);
   km_install_sighandler(SIGTERM, km_signal_passthru);
   km_install_sighandler(SIGHUP, km_signal_passthru);
   km_install_sighandler(SIGQUIT, km_signal_passthru);
   km_install_sighandler(SIGUSR1, km_signal_passthru);
   km_install_sighandler(SIGUSR2, km_signal_passthru);
   km_install_sighandler(SIGWINCH, km_signal_passthru);
   km_install_sighandler(SIGALRM, km_signal_passthru);
   km_install_sighandler(SIGVTALRM, km_signal_passthru);
   km_install_sighandler(SIGPROF, km_signal_passthru);
   km_install_sighandler(SIGCHLD, km_forward_sigchild);

   while (eventfd_write(machine.intr_fd, 1) == -1 && errno == EINTR) {   // unblock gdb loop
      ;   // ignore signals during the write
   }

   km_start_all_vcpus();
   km_vcpus_are_started = 1;
}
