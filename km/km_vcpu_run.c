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

/*
 * run related err and errx - get regs, print RIP and the supplied message
 */
static void
__run_err(void (*fn)(int, const char*, __gnuc_va_list), km_vcpu_t* vcpu, int s, const char* f, ...)
{
   static const char fx[] = "RIP 0x%0llx, RSP 0x%0llx: ";
   int save_errno = errno;
   va_list args;
   kvm_regs_t regs;
   char fmt[strlen(f) + strlen(fx) + 2 * strlen("1234567890123456") + 10];

   va_start(args, f);

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      (*fn)(s, f, args);
      va_end(args);
   }

   sprintf(fmt, fx, regs.rip, regs.rsp);
   strcat(fmt, f);

   errno = save_errno;
   (*fn)(s, fmt, args);
   va_end(args);
}

#define run_err(__s, __f, ...) __run_err(&verr, vcpu, __s, __f, ##__VA_ARGS__)
#define run_errx(__s, __f, ...) __run_err(&verrx, vcpu, __s, __f, ##__VA_ARGS__)

/*
 * return non-zero and set status if guest halted
 */
static int hypercall(km_vcpu_t* vcpu, int* hc, int* status)
{
   kvm_run_t* r = vcpu->cpu_run;
   uint64_t ga;

   /* Sanity checks */
   *hc = r->io.port - KM_HCALL_PORT_BASE;
   if (!(r->io.direction == KVM_EXIT_IO_OUT || r->io.size == 4 || *hc >= 0 || *hc < KM_MAX_HCALL)) {
      run_errx(1,
               "KVM: unexpected IO port activity, port 0x%x 0x%x bytes %s",
               r->io.port,
               r->io.size,
               r->io.direction == KVM_EXIT_IO_OUT ? "out" : "in");
   }
   if (km_hcalls_table[*hc] == NULL) {
      run_errx(1, "KVM: unexpected hypercall 0x%lx", *hc);
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
   static const uint64_t stack_top_high = GUEST_STACK_TOP & ~0xfffffffful;
   /* Recover high 4 bytes, but check for roll under 4GB boundary */
   ga = *(uint32_t*)((void*)r + r->io.data_offset) | stack_top_high;
   if (ga > GUEST_STACK_TOP) {
      ga -= 4 * GIB;
   }
   return km_hcalls_table[*hc](*hc, km_gva_to_kml(ga), status);
}

void km_vcpu_run(km_vcpu_t* vcpu)
{
   int status, hc;
   int wait_for_gdb = km_gdb_enabled();

   while (1) {
      if (wait_for_gdb) {
         km_gdb_prepare_for_run(vcpu);
      }
      if (ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL) < 0) {
         km_info("ioctl exit with errno");
         if (errno == EAGAIN) {
            continue;
         }
         if (errno != EINTR) {
            run_err(1, "KVM: vcpu run failed");
         }
      }
      if (km_gdb_enabled()) {
         if (km_gdb_needs_to_handle_kvm_exit(vcpu)) {
            km_gdb_ask_stub_to_handle_kvm_exit(vcpu, errno);
            wait_for_gdb = 1;
            continue;
         }
         wait_for_gdb = 0;
      }

      switch (vcpu->cpu_run->exit_reason) {
         case KVM_EXIT_IO: /* Hypercall */
            if (hypercall(vcpu, &hc, &status) != 0) {
               run_errx(0, "KVM: hypercall 0x%x stop, status 0x%x", hc, status);
               return;
            }
            break;   // continue the while

         case KVM_EXIT_UNKNOWN:
            run_errx(1, "KVM: unknown err 0x%lx", vcpu->cpu_run->hw.hardware_exit_reason);
            break;

         case KVM_EXIT_FAIL_ENTRY:
            run_errx(1,
                     "KVM: fail entry 0x%lx",
                     vcpu->cpu_run->fail_entry.hardware_entry_failure_reason);
            break;

         case KVM_EXIT_INTERNAL_ERROR:
            run_errx(1, "KVM: internal error, suberr 0x%x", vcpu->cpu_run->internal.suberror);
            break;

         case KVM_EXIT_SHUTDOWN:
            run_errx(1, "KVM: shutdown");
            break;

         case KVM_EXIT_DEBUG:
            if (km_gdb_enabled()) {
               assert("GDB should have taken care of this exit!" == NULL);
            }
            warnx("KVM_EXIT_DEBUG in non-gdb mode. Expected if gdb just "
                  "disconnected. Ignoring...");
            break;

         case KVM_EXIT_INTR:
            // we could get here if VM run is interrupted by a signal to vcpu thread
            // and thread blocks the signal (while KVM does not).
            assert(errno == EINTR);
            if (km_gdb_enabled()) {
               assert("GDB should have taken care of this exit!" == NULL);
            }
            run_errx(1, "KVM: cpu stopped - INTR");
            break;

         case KVM_EXIT_HLT:
            run_errx(1, "KVM: cpu stopped with 'hlt' instruction");
            break;

         default:
            run_errx(1, "KVM: exit 0x%lx", vcpu->cpu_run->exit_reason);
            break;
      }
   }
}