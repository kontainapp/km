/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

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
#include "km_gdb.h"
#include "km_hcalls.h"
#include "km_mem.h"

/*
 * run related err and errx - get regs, print RIP and the supplied message
 */
static void
__run_err(void (*fn)(int, const char*, __gnuc_va_list), km_vcpu_t* vcpu, int s, const char* f, ...)
    __attribute__((format(printf, 4, 5)));   // define attributes
static void
__run_err(void (*fn)(int, const char*, __gnuc_va_list), km_vcpu_t* vcpu, int s, const char* f, ...)
{
   static const char fx[] = "VCPU %d RIP 0x%0llx RSP 0x%0llx ";
   int save_errno = errno;
   va_list args;
   kvm_regs_t regs;
   char fmt[strlen(f) + strlen(fx) + 2 * strlen("1234567890123456") + 64];

   va_start(args, f);

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      (*fn)(s, f, args);
      va_end(args);
   }

   sprintf(fmt, fx, vcpu->vcpu_id, regs.rip, regs.rsp);
   strcat(fmt, f);

   errno = save_errno;
   (*fn)(s, fmt, args);
   va_end(args);
}

#define run_err(__s, __f, ...) __run_err(&verr, vcpu, __s, __f, ##__VA_ARGS__)
#define run_errx(__s, __f, ...) __run_err(&verrx, vcpu, __s, __f, ##__VA_ARGS__)

static void __run_warn(void (*fn)(const char*, __gnuc_va_list), km_vcpu_t* vcpu, const char* f, ...)
    __attribute__((format(printf, 3, 4)));   // define attributes
static void __run_warn(void (*fn)(const char*, __gnuc_va_list), km_vcpu_t* vcpu, const char* f, ...)
{
   static const char fx[] = "VCPU %d RIP 0x%0llx RSP 0x%0llx ";
   va_list args;
   kvm_regs_t regs;
   char fmt[strlen(f) + strlen(fx) + 2 * strlen("1234567890123456") + 64];

   va_start(args, f);

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      (*fn)(f, args);
      va_end(args);
      return;
   }

   sprintf(fmt, fx, vcpu->vcpu_id, regs.rip, regs.rsp);
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

/*
 * return non-zero and set status if guest halted
 */
static int hypercall(km_vcpu_t* vcpu, int* hc, int* status)
{
   kvm_run_t* r = vcpu->cpu_run;
   km_gva_t ga;

   /* Sanity checks */
   *hc = r->io.port - KM_HCALL_PORT_BASE;
   if (!(r->io.direction == KVM_EXIT_IO_OUT && r->io.size == 4 && *hc >= 0 && *hc < KM_MAX_HCALL)) {
      run_errx(1,
               "KVM: unexpected IO port activity, port 0x%x 0x%x bytes %s",
               r->io.port,
               r->io.size,
               r->io.direction == KVM_EXIT_IO_OUT ? "out" : "in");
   }
   if (km_hcalls_table[*hc] == NULL) {
      run_errx(1, "KVM: unexpected hypercall %d", *hc);
   }
   /*
    * Hcall via OUTL only passes 4 bytes, but we need to recover full 8 bytes of
    * the args address. Two assumptions made here: hcall args passed are on
    * stack in the guest, and the stack is less than 4GB long, i.e. the address
    * is withint 4GB range below the top of the stack.
    *
    * TODO: Has to be on each vcpu thread stack
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
   return km_hcalls_table[*hc](vcpu, *hc, km_gva_to_kma(ga), status);
}

static void km_vcpu_exit(km_vcpu_t* vcpu, int s) __attribute__((noreturn));
static void km_vcpu_exit(km_vcpu_t* vcpu, int s)
{
   vcpu->is_paused = 1;   // in case someone else wants to pause this one, no need
   km_vcpu_stopped(vcpu);
   machine.ret = s & 0377;   // Set status, in case this is the last thread. &0377 is per 'man 3 exit'
   pthread_exit((void*)(uint64_t)s);
}

/*
 * Force a vcpu exit and cleanup.
 * Called when we are done with the current thread, and we don’t care for remants to hang around and
 * block fini(). So we make it detached and exit vcpu, and that takes care of true exit for the
 * current thread.
 */
static int km_vcpu_force_exit(km_vcpu_t* vcpu)
{
   km_vcpu_detach(vcpu);
   km_vcpu_exit(vcpu, -1);
   return 0;
}

static void km_vcpu_exit_all(km_vcpu_t* vcpu, int s) __attribute__((noreturn));
static void km_vcpu_exit_all(km_vcpu_t* vcpu, int s)
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
   // TODO - remove the hack below (if () err() )  when 'robust list' is implemented in KM workload runtime
   if (machine.vm_vcpu_run_cnt > 1) {
      errx(s, "Forcing exit_group() without cleanup");
   }
   km_vcpu_exit(vcpu, s);   // Exit with proper return status. This will exit the current thread
}

/*
 * Signal handler. Used when we want VCPU to stop. The signal causes KVM_RUN exit with -EINTR, so
 * the actual handler is noop - it just needs to exist.
 */
static void km_vcpu_pause_sighandler(int signum_unused)
{
   // NOOP
}

/*
 * Call ioctl(KVM_RUN) once, and handles error return from ioctl.
 * Returns 0 on success -1 on ioctl error (an indication that normal exit_reason handling should be
 * skipped upstairs)
 */
static int km_vcpu_one_kvm_run(km_vcpu_t* vcpu)
{
   if (machine.pause_requested) {   // guarantee an exit right away if we are pausing
      vcpu->cpu_run->immediate_exit = 1;
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL) == 0) {
      return 0;
   }
   run_info("KVM_RUN exit %d (%s) imm_exit=%d %s",
            vcpu->cpu_run->exit_reason,
            kvm_reason_name(vcpu->cpu_run->exit_reason),
            vcpu->cpu_run->immediate_exit,
            strerror(errno));
   switch (errno) {
      case EAGAIN:
         break;

      case EINTR:
         if (machine.exit_group == 1) {   // Interrupt from exit_group() - we are done.
            km_vcpu_force_exit(vcpu);     // Clean up and exit the current  VCPU thread
            assert("Reached the unreachable" == NULL);
         }
         vcpu->cpu_run->immediate_exit = 0;
         vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
         if (km_gdb_is_enabled() == 1) {
            km_gdb_notify_and_wait(vcpu, errno);
         } else {   // e.g. gdb attached to KM and then detached while in ioctl
            warn("KVM_RUN interrupted... continuing");
         }
         break;

      default:
         run_err(1, "KVM: vcpu run failed with errno %d (%s)", errno, strerror(errno));
   }
   assert(vcpu->cpu_run->immediate_exit == 0);
   return -1;
}

void* km_vcpu_run(km_vcpu_t* vcpu)
{
   int status, hc;
   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpu_pause_sighandler);
   vcpu->tid = gettid();

   while (1) {
      int reason;

      if (km_vcpu_one_kvm_run(vcpu) < 0) {
         continue;
      }
      reason = vcpu->cpu_run->exit_reason;   // just to save on code width down the road
      run_infox("KVM: exit reason=%d (%s)", reason, kvm_reason_name(reason));
      switch (reason) {
         case KVM_EXIT_IO:
            switch (hypercall(vcpu, &hc, &status)) {
               case HC_CONTINUE:
                  break;

               case HC_STOP:
                  run_infox("KVM: hypercall %d stop, status 0x%x", hc, status);
                  km_vcpu_exit(vcpu, status);
                  break;

               case HC_ALLSTOP:
                  run_infox("KVM: hypercall %d allstop, status 0x%x", hc, status);
                  km_vcpu_exit_all(vcpu, status);
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
            abort();
            break;

         case KVM_EXIT_DEBUG:
         case KVM_EXIT_EXCEPTION:
         case KVM_EXIT_HLT:
            if (km_gdb_is_enabled() == 1) {
               km_gdb_notify_and_wait(vcpu, errno);
            } else {
               run_warn("KVM: exit vcpu. reason=%d (%s)", reason, kvm_reason_name(reason));
               km_vcpu_exit(vcpu, -1);
            }
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
void* km_vcpu_run_main(void* unused)
{
   km_vcpu_t* vcpu = km_main_vcpu();

   vcpu->tid = gettid();
   if (km_gdb_is_enabled() == 1) {
      while (eventfd_write(gdbstub.intr_eventfd, 1) == -1 && errno == EINTR) {   // unblock gdb loop
         ;   // ignore signals during the write
      }
      km_wait_on_eventfd(vcpu->eventfd);   // wait for gbd main loop to allow main vcpu to run
      km_infox(KM_TRACE_VCPU, "%s: vcpu_run VCPU %d unblocked by gdb", __FUNCTION__, vcpu->vcpu_id);
   }
   return km_vcpu_run(vcpu);
}
