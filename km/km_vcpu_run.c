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
      if (g_km_info_verbose)                                                                       \
         __run_warn(&vwarn, vcpu, __f, ##__VA_ARGS__);                                             \
   } while (0)
#define run_infox(__f, ...)                                                                        \
   do {                                                                                            \
      if (g_km_info_verbose)                                                                       \
         __run_warn(&vwarnx, vcpu, __f, ##__VA_ARGS__);                                            \
   } while (0)

static const char* kvm_reason_name(int reason)
{
#define __KVM_REASON_MAX (KVM_EXIT_HYPERV + 1)   // this is the current max
#define __KVM_REASON_NAME(__r) [__r] = #__r
   static const char* const reasons[__KVM_REASON_MAX] = {
       __KVM_REASON_NAME(KVM_EXIT_UNKNOWN),
       __KVM_REASON_NAME(KVM_EXIT_EXCEPTION),
       __KVM_REASON_NAME(KVM_EXIT_IO),
       __KVM_REASON_NAME(KVM_EXIT_HYPERCALL),
       __KVM_REASON_NAME(KVM_EXIT_DEBUG),
       __KVM_REASON_NAME(KVM_EXIT_HLT),
       __KVM_REASON_NAME(KVM_EXIT_MMIO),
       __KVM_REASON_NAME(KVM_EXIT_IRQ_WINDOW_OPEN),
       __KVM_REASON_NAME(KVM_EXIT_SHUTDOWN),
       __KVM_REASON_NAME(KVM_EXIT_FAIL_ENTRY),
       __KVM_REASON_NAME(KVM_EXIT_INTR),
       __KVM_REASON_NAME(KVM_EXIT_SET_TPR),
       __KVM_REASON_NAME(KVM_EXIT_TPR_ACCESS),
       __KVM_REASON_NAME(KVM_EXIT_S390_SIEIC),
       __KVM_REASON_NAME(KVM_EXIT_S390_RESET),
       __KVM_REASON_NAME(KVM_EXIT_DCR), /* deprecated */
       __KVM_REASON_NAME(KVM_EXIT_NMI),
       __KVM_REASON_NAME(KVM_EXIT_INTERNAL_ERROR),
       __KVM_REASON_NAME(KVM_EXIT_OSI),
       __KVM_REASON_NAME(KVM_EXIT_PAPR_HCALL),
       __KVM_REASON_NAME(KVM_EXIT_S390_UCONTROL),
       __KVM_REASON_NAME(KVM_EXIT_WATCHDOG),
       __KVM_REASON_NAME(KVM_EXIT_S390_TSCH),
       __KVM_REASON_NAME(KVM_EXIT_EPR),
       __KVM_REASON_NAME(KVM_EXIT_SYSTEM_EVENT),
       __KVM_REASON_NAME(KVM_EXIT_S390_STSI),
       __KVM_REASON_NAME(KVM_EXIT_IOAPIC_EOI),
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
   if (km_gdb_is_enabled()) {
      vcpu->cpu_run->exit_reason = KVM_EXIT_HLT;
      km_gdb_notify_and_wait(vcpu, errno);   // TODO: just send "thread exited" event to gdb
   }
   km_vcpu_stopped(vcpu);
   machine.ret = s & 0377;   // Process status, if this is the last thread. &0377 is per 'man 3 exit'
   pthread_exit((void*)(uint64_t)s);
}

static void km_vcpu_exit_all(km_vcpu_t* vcpu, int s) __attribute__((noreturn));
static void km_vcpu_exit_all(km_vcpu_t* vcpu, int s)
{
   machine.pause_requested = 1;
   vcpu->is_paused = 1;
   km_vcpu_apply_all(km_vcpu_pause, NULL);
   km_vcpu_wait_for_all_to_pause();
   if (km_gdb_is_enabled()) {
      // TODO: just send "happyexit" to gdb
   }
   machine.ret = s & 0377;   // Process status. &0377 is per 'man 3 exit'
   km_vcpu_exit(vcpu, s);
}

/*
 * Signal handler. Used when we want VCPU to stop. Upstairs we set immediate_exit to 1
 * and then signal the thread, which causes interrupt and reenter to KVM_RUN with
 * immediate exit. So the actual handler is noop - it just need to exist.
 */
static void km_vcpu_pause_sighandler(int signum_unused)
{
   // NOOP
}

void* km_vcpu_run(km_vcpu_t* vcpu)
{
   int status, hc;
   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpu_pause_sighandler);

   while (1) {
      if (ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL) < 0) {
         run_info("ioctl exit with reason %d (%s) imm_exit=%d errno %d",
                  vcpu->cpu_run->exit_reason,
                  kvm_reason_name(vcpu->cpu_run->exit_reason),
                  vcpu->cpu_run->immediate_exit,
                  errno);
         if (errno == EAGAIN) {
            continue;
         }
         if (errno != EINTR) {
            run_err(1, "KVM: vcpu run failed with errno %d (%s)", errno, strerror(errno));
         }
      }
      int reason = vcpu->cpu_run->exit_reason;   // just to save on code width down the road
      switch (reason) {
         case KVM_EXIT_IO:          // Exiting from hypercall
            if (errno == EINTR) {   // hypercall was interrupted
               assert(vcpu->cpu_run->immediate_exit == 1);
               km_infox("KVM_EXIT_IO: HC interrupted. Time to stop VCPU %d", vcpu->vcpu_id);
               if (km_gdb_is_enabled()) {
                  vcpu->is_paused = 1;
                  vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
                  km_gdb_notify_and_wait(vcpu, errno);
               } else {
                  /*
                   * We are supposed to only get here on hypercall interrupt by a signal outside of
                   * GDB, i.e. on exit_grp(). So we are done with the current  thread, and we don’t
                   * care for remants to hang around and block fini(). So we make it detached and
                   * exit vcpu, and that takes care of true exit for the current thread.
                   */
                  km_vcpu_detach(vcpu);   // detach so the next line does true exit
                  km_vcpu_exit(vcpu, EINTR);
               }
               break;
            }
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

         case KVM_EXIT_HLT:
            km_infox("KVM: vcpu HLT");
            km_vcpu_exit(vcpu, status);
            break;

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

         case KVM_EXIT_INTR:
            if (km_gdb_is_enabled()) {
               km_gdb_notify_and_wait(vcpu, errno);
            } else {
               km_vcpu_exit(vcpu, EINTR);
            }
            break;

         case KVM_EXIT_DEBUG:
         case KVM_EXIT_EXCEPTION:
            if (km_gdb_is_enabled()) {
               km_gdb_notify_and_wait(vcpu, errno);
            } else {
               run_warn("KVM: stopped. reason=%d (%s)", reason, kvm_reason_name(reason));
               km_vcpu_exit(vcpu, -1);
            }
            break;

         default:
            run_errx(1, "KVM: exit. reason=%d (%s)", reason, kvm_reason_name(reason));
            break;
      }
   }
}

void* km_vcpu_run_main(void* unused)
{
   km_vcpu_t* vcpu = km_main_vcpu();

   /*
    * Main vcpu in presence of gdb needs to pause before entering guest main() and wait for gdb
    * client connection. The client will control the execution by continue or step commands.
    */
   if (km_gdb_is_enabled()) {
      km_gdb_prepare_for_run(vcpu);
   }
   return km_vcpu_run(vcpu);   // and now go into the run loop
}
