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
{
   static const char fx[] = "VCPU %d, RIP 0x%0llx, RSP 0x%0llx: ";
   int save_errno = errno;
   va_list args;
   kvm_regs_t regs;
   char fmt[strlen(f) + strlen(fx) + 2 * strlen("1234567890123456") + 10];

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
{
   static const char fx[] = "VCPU %d, RIP 0x%0llx, RSP 0x%0llx: ";
   va_list args;
   kvm_regs_t regs;
   char fmt[strlen(f) + strlen(fx) + 2 * strlen("1234567890123456") + 10];

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

/*
 * return non-zero and set status if guest halted
 */
static int hypercall(km_vcpu_t* vcpu, int* hc, int* status)
{
   kvm_run_t* r = vcpu->cpu_run;
   km_gva_t ga;

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
      run_errx(1, "KVM: unexpected hypercall %ld", *hc);
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
   return km_hcalls_table[*hc](*hc, km_gva_to_kma(ga), status);
}

void* km_vcpu_run(km_vcpu_t* vcpu)
{
   int status, hc;

   if (km_gdb_enabled()) {
      // unblock signal in the VM (and block on the current thead)
      km_vcpu_unblock_signal(vcpu, GDBSTUB_SIGNAL);
   }
   while (1) {
      if (ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL) < 0) {
         km_info("ioctl exit with errno");
         if (errno == EAGAIN) {
            continue;
         }
         if (errno != EINTR) {
            run_err(1, "KVM: vcpu run failed");
         }
      }
      switch (vcpu->cpu_run->exit_reason) {
         case KVM_EXIT_IO: /* Hypercall */
            if (hypercall(vcpu, &hc, &status) != 0) {
               run_warn("KVM: hypercall %d stop, status 0x%x", hc, status);
               if (km_gdb_enabled()) {
                  vcpu->cpu_run->exit_reason = KVM_EXIT_HLT;
                  km_gdb_ask_stub_to_handle_kvm_exit(vcpu, errno);
               }
               km_vcpu_stopped(vcpu);
               return (void*)(uint64_t)status;   // from the vcpu thread
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
         case KVM_EXIT_INTR:
         case KVM_EXIT_HLT:
         case KVM_EXIT_EXCEPTION:
            if (km_gdb_enabled()) {
               km_gdb_ask_stub_to_handle_kvm_exit(vcpu, errno);
            } else {
               run_errx(1, "KVM: cpu stopped with %d reason", vcpu->cpu_run->exit_reason);
            }
            break;

         default:
            run_errx(1, "KVM: exit 0x%lx", vcpu->cpu_run->exit_reason);
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
   if (km_gdb_enabled()) {
      km_gdb_prepare_for_run(vcpu);
   }
   return km_vcpu_run(vcpu);   // and now go into the run loop
}
