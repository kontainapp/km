/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "km.h"
#include "km_coredump.h"
#include "km_filesys.h"
#include "km_gdb.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "km_signal.h"

int vcpu_dump = 0;

/*
 * run related err and errx - get regs, print RIP and the supplied message
 */
static void
__run_err(void (*fn)(int, const char*, __builtin_va_list), km_vcpu_t* vcpu, int s, const char* f, ...)
    __attribute__((format(printf, 4, 5)));   // define attributes
static void
__run_err(void (*fn)(int, const char*, __builtin_va_list), km_vcpu_t* vcpu, int s, const char* f, ...)
{
   static const char fx[] = "VCPU %d RIP 0x%0llx RSP 0x%0llx CR2 0x%llx ";
   int save_errno = errno;
   va_list args;
   char fmt[strlen(f) + strlen(fx) + 3 * strlen("1234567890123456") + 64];

   va_start(args, f);

   sprintf(fmt, fx, vcpu->vcpu_id, vcpu->regs.rip, vcpu->regs.rsp, vcpu->sregs.cr2);
   strcat(fmt, f);

   errno = save_errno;
   (*fn)(s, fmt, args);
   va_end(args);
}

#define run_err(__s, __f, ...) __run_err(&verr, vcpu, __s, __f, ##__VA_ARGS__)
#define run_errx(__s, __f, ...) __run_err(&verrx, vcpu, __s, __f, ##__VA_ARGS__)

static void
__run_warn(void (*fn)(const char*, __builtin_va_list), km_vcpu_t* vcpu, const char* f, ...)
    __attribute__((format(printf, 3, 4)));   // define attributes
static void __run_warn(void (*fn)(const char*, __builtin_va_list), km_vcpu_t* vcpu, const char* f, ...)
{
   static const char fx[] = "VCPU %d RIP 0x%0llx RSP 0x%0llx CR2 0x%llx ";
   va_list args;
   char fmt[strlen(f) + strlen(fx) + 3 * strlen("1234567890123456") + 64];

   va_start(args, f);

   sprintf(fmt, fx, vcpu->vcpu_id, vcpu->regs.rip, vcpu->regs.rsp, vcpu->sregs.cr2);
   strcat(fmt, f);

   (*fn)(fmt, args);
   va_end(args);
}

#define run_warn(__f, ...) __run_warn(&vwarnx, vcpu, __f, ##__VA_ARGS__)
// only show this for verbose ('-V') runs
#define run_info(__f, ...)                                                                         \
   do {                                                                                            \
      if (km_trace_enabled() && regexec(&km_info_trace.tags, KM_TRACE_KVM, 0, NULL, 0) == 0)       \
         __run_warn(&vwarn, vcpu, __f, ##__VA_ARGS__);                                             \
   } while (0)
#define run_infox(__f, ...)                                                                        \
   do {                                                                                            \
      if (km_trace_enabled() && regexec(&km_info_trace.tags, KM_TRACE_KVM, 0, NULL, 0) == 0)       \
         __run_warn(&vwarnx, vcpu, __f, ##__VA_ARGS__);                                            \
   } while (0)

static const char* kvm_reason_name(int reason)
{
#define __KVM_REASON_MAX (KVM_EXIT_HYPERV + 1)   // this is the current max
#define __KVM_REASON_NAME(__r) [__r] = #__r
   static const char* const reasons[__KVM_REASON_MAX] = {
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
   kvm_regs_t regs;
   char* cur = buf;
   size_t rem = len;

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      fprintf(stderr, "KVM_GET_REGS failed: %d - %s\n", errno, strerror(errno));
      return NULL;
   }

   FMT_BUF(cur, rem, "== KVM_GET_REGS ==\n");
   FMT_BUF(cur,
           rem,
           "RAX: 0x%-16llx RBX: 0x%-16llx RCX: 0x%-16llx RDX: 0x%-16llx\n",
           regs.rax,
           regs.rbx,
           regs.rcx,
           regs.rdx);
   FMT_BUF(cur,
           rem,
           "RSI: 0x%-16llx RDI: 0x%-16llx RSP: 0x%-16llx RBP: 0x%-16llx\n",
           regs.rsi,
           regs.rdi,
           regs.rsp,
           regs.rbp);
   FMT_BUF(cur,
           rem,
           "R8:  0x%-16llx R9:  0x%-16llx R10: 0x%-16llx R11: 0x%-16llx\n",
           regs.r8,
           regs.r9,
           regs.r10,
           regs.r11);
   FMT_BUF(cur,
           rem,
           "R12: 0x%-16llx R13: 0x%-16llx R14: 0x%-16llx R15: 0x%-16llx\n",
           regs.r12,
           regs.r13,
           regs.r14,
           regs.r15);

   FMT_BUF(cur, rem, "RIP: 0x%-16llx RFLAGS: 0x%llx\n", regs.rip, regs.rflags);
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
   kvm_sregs_t sregs;
   char* cur = buf;
   size_t rem = len;

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
      fprintf(stderr, "KVM_GET_SREGS failed: %d - %s\n", errno, strerror(errno));
      return NULL;
   }
   FMT_BUF(cur, rem, "== KVM_GET_SREGS ==\n");
   FMT_SREG("CS", &sregs.cs, cur, rem);
   FMT_SREG("DS", &sregs.ds, cur, rem);
   FMT_SREG("ES", &sregs.es, cur, rem);
   FMT_SREG("FS", &sregs.fs, cur, rem);
   FMT_SREG("GS", &sregs.gs, cur, rem);
   FMT_SREG("SS", &sregs.ss, cur, rem);
   FMT_SREG("TR", &sregs.tr, cur, rem);
   FMT_SREG("LDT", &sregs.ldt, cur, rem);
   FMT_BUF(cur, rem, "GDT base: 0x%llx limit: 0x%x\n", sregs.gdt.base, sregs.gdt.limit);
   FMT_BUF(cur, rem, "IDT base: 0x%llx limit: 0x%x\n", sregs.idt.base, sregs.idt.limit);
   FMT_BUF(cur,
           rem,
           "CR0: 0x%llx CR2: 0x%llx CR3: 0x%llx CR4: 0x%llx CR8: 0x%llx\n",
           sregs.cr0,
           sregs.cr2,
           sregs.cr3,
           sregs.cr4,
           sregs.cr8);
   FMT_BUF(cur, rem, "EFER: 0x%llx APIC_BASE: 0x%llxc\n", sregs.efer, sregs.apic_base);

   FMT_BUF(cur, rem, "INTERRUPT BITMAP:");

   for (int i = 0; i < sizeof(sregs.interrupt_bitmap) / sizeof(__u64); i++) {
      FMT_BUF(cur, rem, " 0x%llx", sregs.interrupt_bitmap[i]);
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
      fprintf(stderr, "ERROR - NULL return from dump_events\n");
      return;
   }

   fprintf(stderr, "%s", buf);
}

/*
 * populate vcpu->regs with register values
 */
void km_read_registers(km_vcpu_t* vcpu)
{
   if (vcpu->regs_valid) {
      return;
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &vcpu->regs) < 0) {
      warn("%s - KVM_GET_REGS failed", __FUNCTION__);
      return;
   }
   vcpu->regs_valid = 1;
}

void km_write_registers(km_vcpu_t* vcpu)
{
   if (!vcpu->regs_valid) {
      errx(2, "%s - registers not valid", __FUNCTION__);
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_REGS, &vcpu->regs) < 0) {
      warn("%s - KVM_SET_REGS failed", __FUNCTION__);
      return;
   }
}

/*
 * populate vcpu->sregs with segment register values
 */
void km_read_sregisters(km_vcpu_t* vcpu)
{
   if (vcpu->sregs_valid) {
      return;
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_SREGS, &vcpu->sregs) < 0) {
      warn("%s - KVM_GET_SREGS failed", __FUNCTION__);
      return;
   }
   vcpu->sregs_valid = 1;
}

void km_write_sregisters(km_vcpu_t* vcpu)
{
   if (!vcpu->sregs_valid) {
      errx(2, "%s - sregisters not valid", __FUNCTION__);
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0) {
      warn("%s - KVM_SET_SREGS failed", __FUNCTION__);
      return;
   }
}

/*
 * return non-zero and set status if guest halted
 */
static int hypercall(km_vcpu_t* vcpu, int* hc)
{
   kvm_run_t* r = vcpu->cpu_run;
   km_gva_t ga;

   /* Sanity checks */
   *hc = r->io.port - KM_HCALL_PORT_BASE;
   if (!(r->io.direction == KVM_EXIT_IO_OUT && r->io.size == 4 && *hc >= 0 && *hc < KM_MAX_HCALL)) {
      km_info(KM_TRACE_SIGNALS,
              "KVM: unexpected IO port activity, port 0x%x 0x%x bytes %s",
              r->io.port,
              r->io.size,
              r->io.direction == KVM_EXIT_IO_OUT ? "out" : "in");

      siginfo_t info = {.si_signo = SIGSYS, .si_code = SI_KERNEL};
      km_post_signal(vcpu, &info);
      return -1;
   }
   if (km_hcalls_table[*hc] == NULL) {
      warnx("Unimplemented hypercall %d (%s)", *hc, km_hc_name_get(*hc));
      siginfo_t info = {.si_signo = SIGSYS, .si_code = SI_KERNEL};
      km_post_signal(vcpu, &info);
      return -1;
   }
   /*
    * Hcall via OUTL only passes 4 bytes, but we need to recover full 8 bytes of
    * the args address. Two assumptions made here: hcall args passed are on
    * stack in the guest, and the stack is less than 4GB long, i.e. the address
    * is withint 4GB range below the top of the stack.
    *
    * We set the high four bytes to the same as top of the stack, and check for
    * underflow.
    */
   /* high four bytes */
   km_gva_t stack_top_high = vcpu->stack_top & ~0xfffffffful;
   /* Recover high 4 bytes, but check for roll under 4GB boundary */
   ga = *(uint32_t*)((km_kma_t)r + r->io.data_offset) | stack_top_high;
   if (ga > vcpu->stack_top) {
      ga -= 4 * GIB;
   }
   km_infox(KM_TRACE_HC, "vcpu %d, calling hc = %d (%s)", vcpu->vcpu_id, *hc, km_hc_name_get(*hc));
   km_kma_t ga_kma;
   if ((ga_kma = km_gva_to_kma(ga)) == NULL || km_gva_to_kma(ga + sizeof(km_hc_args_t) - 1) == NULL) {
      km_infox(KM_TRACE_SIGNALS, "%s: hc: %d bad km_hc_args_t address:0x%lx", __FUNCTION__, *hc, ga);
      siginfo_t info = {.si_signo = SIGSYS, .si_code = SI_KERNEL};
      km_post_signal(vcpu, &info);
      return -1;
   }
   return km_hcalls_table[*hc](vcpu, *hc, ga_kma);
}

static void km_vcpu_exit(km_vcpu_t* vcpu)
{
   vcpu->is_paused = 1;   // in case someone else wants to pause this one, no need
   km_vcpu_stopped(vcpu);
}

// static void km_vcpu_exit_all(km_vcpu_t* vcpu, int s) __attribute__((noreturn));
static void km_vcpu_exit_all(km_vcpu_t* vcpu)
{
   machine.pause_requested = 1;   // prevent new vcpus from racing
   machine.exit_group = 1;        // make sure we exit and not waiting for gdb
   vcpu->is_paused = 1;
   km_vcpu_apply_all(km_vcpu_pause, 0);
   km_vcpu_wait_for_all_to_pause();
   /*
    * At this point, some threads may still be in pthread_join(), or may be alive because they were
    * paused (e.g. for stop) before the signal. We need to give them time to clean up.
    * For now, we give them 100 msec. TODO: wait forever after the hack below is removed
    */
   static const struct timespec req = {
       .tv_sec = 0, .tv_nsec = 10000000, /* 10 millisec */
   };
   int count = 10;
   while (machine.vm_vcpu_run_cnt > 1 && count-- > 0) {
      if (km_trace_enabled()) {
         km_infox(KM_TRACE_VCPU,
                  "%s VCPU %d: %d vcpus are still running",
                  __FUNCTION__,
                  vcpu->vcpu_id,
                  machine.vm_vcpu_run_cnt);
         km_vcpu_apply_all(km_vcpu_print, 0);
      }
      nanosleep(&req, NULL);
   }
   // TODO - consider an unforced solution
   if (machine.vm_vcpu_run_cnt > 1) {
      km_infox(KM_TRACE_VCPU, "Forcing exit_group() without cleanup");
      exit(machine.exit_status);
   }
   km_vcpu_exit(vcpu);   // Exit with proper return status. This will exit the current thread
}

/*
 * Signal handler. Used when we want VCPU to stop. The signal causes KVM_RUN exit with -EINTR, so
 * the actual handler is noop - it just needs to exist.
 * Calling km_info() and km_infox() from this function seems to occassionally cause a mutex
 * deadlock in the regular expression code called from km_info*().
 */
static void km_vcpu_pause_sighandler(int signum_unused, siginfo_t* info_unused, void* ucontext_unused)
{
   // NOOP
}

/*
 * Forward a signal file descriptor received by KM into the guest signal system.
 * This relies on info->si_fd being populated.
 */
static void km_forward_fd_signal(int signo, siginfo_t* sinfo, void* ucontext_unused)
{
   int guest_fd = hostfd_to_guestfd(NULL, sinfo->si_fd);
   if (guest_fd < 0) {
      return;
   }
   siginfo_t info = {.si_signo = signo, .si_code = SI_KERNEL};
   km_post_signal(NULL, &info);
}

/*
 * Call ioctl(KVM_RUN) once, and handles error return from ioctl.
 * Returns 0 on success -1 on ioctl error (an indication that normal exit_reason handling should be
 * skipped upstairs)
 */
static int km_vcpu_one_kvm_run(km_vcpu_t* vcpu)
{
   int rc;

   if (machine.pause_requested) {   // guarantee an exit right away if we are pausing
      vcpu->cpu_run->immediate_exit = 1;
   }

   /*
    * When ioctl( KVM_RUN ) fails, it apparently doesn't set exit_reason.
    * To avoid seeing the exit_reason from the preceeding KVM_RUN ioctl
    * we initialize the value before making the ioctl() request.
    */
   vcpu->cpu_run->exit_reason = 0;   // i hope this won't disturb kvm.
   km_infox(KM_TRACE_VCPU,
            "vcpu %d, is_paused %d, about to ioctl( KVM_RUN )",
            vcpu->vcpu_id,
            vcpu->is_paused);
   rc = ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL);
   km_infox(KM_TRACE_VCPU,
            "vcpu %d, is_paused %d, ioctl( KVM_RUN ) returned %d",
            vcpu->vcpu_id,
            vcpu->is_paused,
            rc);

   // If we need them, harvest the registers once upon return.
   if (km_trace_enabled() || km_gdb_is_enabled()) {
      km_read_registers(vcpu);
      km_read_sregisters(vcpu);
   }

   if (rc == 0) {
      return 0;
   }
   run_info("KVM_RUN exit %d (%s) imm_exit=%d",
            vcpu->cpu_run->exit_reason,
            kvm_reason_name(vcpu->cpu_run->exit_reason),
            vcpu->cpu_run->immediate_exit);
   switch (errno) {
      case EAGAIN:
         vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
         break;

      case EINTR:
         if (machine.exit_group == 1) {   // Interrupt from exit_group() - we are done.
            km_vcpu_exit(vcpu);           // Clean up and exit the current VCPU thread
            assert("Reached the unreachable" == NULL);
         }
         vcpu->cpu_run->immediate_exit = 0;
         vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
         if (km_gdb_is_enabled() == 1) {
            siginfo_t info;
            km_dequeue_signal(vcpu, &info);
            if (info.si_signo == 0) {
               /*
                * ioctl( KVM_RUN ) was interrupted by SIGUSR1
                * All we need to do here is arrange to pause this vcpu
                * and that will happen in the caller.
                */
               machine.pause_requested = 1;
               return -1;
            } else {
               km_gdb_notify_and_wait(vcpu, info.si_signo, true);
            }
         }
         break;

      case EFAULT: {
         /*
          * This happens when the guest violates memory protection, for example
          * writes to the text area. This is a side-effect of how we protect
          * guest memory (guest PT says page is writable, but kernel says it
          * isn't).
          */
         vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
         siginfo_t info = {.si_signo = SIGSEGV, .si_code = SI_KERNEL};
         km_post_signal(vcpu, &info);
         break;
      }
      default:
         run_err(1, "KVM: vcpu run failed with errno %d (%s)", errno, strerror(errno));
   }
   /*
    * If there is a signal with a handler, setup the guest's registers to
    * execute the handler in the the KVM_RUN.
    */
   km_deliver_signal(vcpu);
   assert(vcpu->cpu_run->immediate_exit == 0);
   return -1;
}

/*
 * Return 1 if the passed vcpu has been running or stepping.
 * Return 0 if the passed vcpu has been paused.
 * Skip the vcpu whose id matches me.
 */
int km_vcpu_is_running(km_vcpu_t* vcpu, uint64_t me)
{
   if (vcpu == (km_vcpu_t*)me) {
      // Don't count this vcpu
      return 0;
   }
   return (vcpu->gdb_vcpu_state.gvs_gdb_run_state == GRS_PAUSED) ? 0 : 1;
}

/*
 * If the passed vcpu is allocated return 1, else return 0.
 */
static int km_vcpu_count_allocated(km_vcpu_t* vcpu, uint64_t unused)
{
   return vcpu->is_used != 0;
}

void* km_vcpu_run(km_vcpu_t* vcpu)
{
   int hc;
   vcpu->is_paused = 1;

   char thread_name[16];   // see 'man pthread_getname_np'
   sprintf(thread_name, "vcpu-%d", vcpu->vcpu_id);
   pthread_setname_np(vcpu->vcpu_thread, thread_name);

   while (1) {
      int reason;

      /*
       * Interlock with machine.pause_requested.
       * Also handle the condition where a thread was in hypercall which returned
       * after gdb had marked the thread as paused.  We need to block the thread.
       */
      if (machine.pause_requested ||
          (km_gdb_is_enabled() != 0 && vcpu->gdb_vcpu_state.gvs_gdb_run_state == GRS_PAUSED)) {
         km_infox(KM_TRACE_VCPU,
                  "%s: vcpu %d, pause_requested %d, gvs_gdb_run_state %d, blocking on gdb_efd",
                  __FUNCTION__,
                  vcpu->vcpu_id,
                  machine.pause_requested,
                  vcpu->gdb_vcpu_state.gvs_gdb_run_state);
         vcpu->is_paused = 1;
         km_read_registers(vcpu);
         km_read_sregisters(vcpu);
         km_wait_on_gdb_cv(vcpu);
         km_infox(KM_TRACE_VCPU,
                  "%s: vcpu %d unblocked, pause_requested %d",
                  __FUNCTION__,
                  vcpu->vcpu_id,
                  machine.pause_requested);
      }

      /*
       * If there is a signal ready to go, tell KVM to do an immediate exit.
       */
      if (km_signal_ready(vcpu)) {
         vcpu->cpu_run->immediate_exit = 1;
      }

      // Invalidate cached registers
      vcpu->regs_valid = 0;
      vcpu->sregs_valid = 0;
      vcpu->is_paused = 0;
      if (km_vcpu_one_kvm_run(vcpu) < 0) {
         vcpu->is_paused = 1;
         continue;
      }
      vcpu->is_paused = 1;
      reason = vcpu->cpu_run->exit_reason;   // just to save on code width down the road
      run_infox("KVM: exit reason=%d (%s)", reason, kvm_reason_name(reason));
      switch (reason) {
         case KVM_EXIT_IO:
            switch (hypercall(vcpu, &hc)) {
               case HC_CONTINUE:
                  km_infox(KM_TRACE_VCPU,
                           "vcpu %d, return from hc = %d (%s), gdb_run_state %d, pause_requested "
                           "%d, is_paused %d",
                           vcpu->vcpu_id,
                           hc,
                           km_hc_name_get(hc),
                           vcpu->gdb_vcpu_state.gvs_gdb_run_state,
                           machine.pause_requested,
                           vcpu->is_paused);
                  break;

               case HC_STOP:
                  /*
                   * This thread has executed pthread_exit() or SYS_exit and is terminating.
                   * If there is more than one thread and all of the other threads are
                   * paused we need to let gdb know that this thread is gone so that it
                   * can give the user a chance to get at least one of the other threads
                   * going.
                   * We need to wake gdb before calling km_vcpu_exit() because km_vcpu_exit()
                   * will block until a new thread is created and reuses this vcpu.
                   */
                  run_infox("KVM: hypercall %d stop", hc);
                  if (km_gdb_is_enabled() != 0) {
                     if (km_vcpu_apply_all(km_vcpu_count_allocated, 0) > 1 &&
                         km_vcpu_apply_all(km_vcpu_is_running, (uint64_t)vcpu) == 0) {
                        /*
                         * We notify gdb but don't wait because we need to go on and park the
                         * vcpu by calling km_vcpu_exit().  Note that the gdb server
                         * sometimes depends on knowing a vcpu is idle, so we do have a bit
                         * of a race here if the gdb server wakes up before this thread can
                         * mark the vcpu as unused.  Should we set vcpu->is_used = 0 here?
                         */
                        vcpu->cpu_run->exit_reason = KVM_EXIT_DEBUG;
                        km_gdb_notify_and_wait(vcpu, GDB_KMSIGNAL_THREADEXIT, false /* don't wait */);
                     }
                  }
                  km_vcpu_exit(vcpu);
                  break;

               case HC_ALLSTOP:
                  // This thread has executed exit_group() and the payload is terminating.
                  run_infox("KVM: hypercall %d allstop, status 0x%x", hc, machine.exit_status);
                  km_vcpu_exit_all(vcpu);
                  break;
            }
            break;   // exit_reason, case KVM_EXIT_IO

         case KVM_EXIT_UNKNOWN:
            run_errx(1, "KVM: unknown err 0x%llx", vcpu->cpu_run->hw.hardware_exit_reason);
            break;

         case KVM_EXIT_FAIL_ENTRY:
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

         case KVM_EXIT_DEBUG:
            if (km_gdb_is_enabled() == 1) {
               /*
                * We handle stepping through a range of addresses here.
                * If we are in the address stepping range, we just turn around and keep
                * stepping.  Once we exit the address range we give control to the
                * gdb server thread.
                * If we hit a breakpoint planted in the stepping range we exit to gdb.
                */
               if (vcpu->gdb_vcpu_state.gvs_gdb_run_state == GRS_RANGESTEPPING &&
                   vcpu->cpu_run->debug.arch.pc >= vcpu->gdb_vcpu_state.gvs_steprange_start &&
                   vcpu->cpu_run->debug.arch.pc < vcpu->gdb_vcpu_state.gvs_steprange_end &&
                   (vcpu->cpu_run->debug.arch.dr6 & 0x4000) != 0) {
                  continue;
               }
               /*
                * To find out which breakpoint fired we need to look into this processors kvm_run
                * structure. In particular the kvm_debug_exit_arch structure. We use the pseudo
                * signal GDB_KMSIGNAL_KVMEXIT to cause the gdb payload handler to look into the
                * vcpu's kvm_run structure to figure out what has happened so that it can generate
                * the correct gdb stop reply.
                */
               km_gdb_notify_and_wait(vcpu, GDB_KMSIGNAL_KVMEXIT, true);
            } else {
               // gdb is not attached, we shouldn't be seeing a debug exit?
               run_warn("KVM: vcpu debug exit without gdb?");
               km_vcpu_exit(vcpu);
            }
            break;

         case KVM_EXIT_EXCEPTION:
            if (km_gdb_is_enabled() == 1) {
               km_gdb_notify_and_wait(vcpu, km_signal_ready(vcpu), true);
            } else {
               run_warn("KVM: exit vcpu. reason=%d (%s)", reason, kvm_reason_name(reason));
               km_vcpu_exit(vcpu);
            }
            break;

         case KVM_EXIT_HLT:
            warnx("KVM: KVM_EXIT_HLT - exiting the thread");
            km_vcpu_exit(vcpu);
            break;

         default:
            run_errx(1, "KVM: exit. reason=%d (%s)", reason, kvm_reason_name(reason));
            break;
      }
   }
}

/*
 * Main vcpu in presence of gdb needs to pause before entering guest main() and wait for gdb
 * client connection. The client will control the execution by continue or step commands.
 */
void* km_vcpu_run_main(km_vcpu_t* unused)
{
   km_vcpu_t* vcpu = km_main_vcpu();

   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpu_pause_sighandler);
   km_install_sighandler(SIGPIPE, km_forward_fd_signal);
   km_install_sighandler(SIGIO, km_forward_fd_signal);

   if (km_gdb_is_enabled() == 1) {
      while (eventfd_write(machine.intr_fd, 1) == -1 && errno == EINTR) {   // unblock gdb loop
         ;   // ignore signals during the write
      }
      km_wait_on_gdb_cv(vcpu);   // wait for gbd main loop to allow main vcpu to run
      km_infox(KM_TRACE_VCPU, "%s: vcpu_run VCPU %d unblocked by gdb", __FUNCTION__, vcpu->vcpu_id);
   }
   return km_vcpu_run(vcpu);
}
