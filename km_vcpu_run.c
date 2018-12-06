/*
 * TODO: Header
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>

#include "km_hcalls.h"
#include "km.h"

/*
 * run related err and errx - get regs, print RIP and the supplied message
 */
static void __run_err(void (*fn)(int, const char *, __gnuc_va_list),
                      km_vcpu_t *vcpu, int s, const char *f, ...)
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
 * return non-zero if guest halted, status in RAX
 */
static int hypercall(km_vcpu_t *vcpu, int *status)
{
   kvm_run_t *r = vcpu->cpu_run;
   km_hcall_t hc = r->io.port - KM_HCALL_PORT_BASE;
   uint64_t ga;
   int rc;

   /* TODO: Can we send more than 4 bytes in one exit? */
   /* Sanity checks */
   if (!(r->io.direction == KVM_EXIT_IO_OUT || r->io.size == 4 ||
         hc >= KM_HC_BASE || hc < KM_HC_COUNT)) {
      run_errx(1, "KVM: unexpected IO port activity, port 0x%x 0x%x bytes %s",
               r->io.port, r->io.size,
               r->io.direction == KVM_EXIT_IO_OUT ? "out" : "in");
   }
   if (km_hcalls_table[hc] == NULL) {
      run_errx(1, "KVM: unexpected hypercall 0x%lx", hc);
   }
   ga = *(uint32_t *)((void *)r + r->io.data_offset);
   if ((rc = km_hcalls_table[hc](km_gva_to_kma(ga), status)) != 0) {
      printf("KVM: hypercall 0x%x stops, status 0x%x\n", hc, *status);
   }
   return rc;
}

void km_vcpu_run(km_vcpu_t *vcpu)
{
   int status;

   while (1) {
      if (ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL) < 0) {
         if (errno == EINTR || errno == EAGAIN) {
            continue;
         }
         if (errno == EFAULT) {
            /* TODO: Can this happen? What about exit_reason in this case? */
            run_errx(1, "KVM: guest segfault");
         }
         run_err(1, "KVM: vcpu run failed");
      }
      switch (vcpu->cpu_run->exit_reason) {
         case KVM_EXIT_IO: /* Hypercall */
            if (hypercall(vcpu, &status) != 0) {
               run_errx(1, "KVM: stop, status 0x%x", status);
               return;
            }
            break;       // continue the while

         case KVM_EXIT_UNKNOWN:
            run_errx(1, "KVM: unknown err 0x%lx",
                     vcpu->cpu_run->hw.hardware_exit_reason);
            break;

         case KVM_EXIT_FAIL_ENTRY:
            run_errx(1, "KVM: fail entry 0x%lx",
                     vcpu->cpu_run->fail_entry.hardware_entry_failure_reason);
            break;

         case KVM_EXIT_DEBUG:
            run_errx(1, "KVM: debug isn't implemented yet");
            break;

         case KVM_EXIT_INTERNAL_ERROR:
            run_errx(1, "KVM: internal error, suberr 0x%x",
                     vcpu->cpu_run->internal.suberror);
            break;

         case KVM_EXIT_SHUTDOWN:
            run_errx(1, "KVM: shutdown");
            break;

         default:
            run_errx(1, "KVM: exit 0x%lx", vcpu->cpu_run->exit_reason);
            break;
      }
   }
}