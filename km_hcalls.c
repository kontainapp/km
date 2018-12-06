/*
 * TODO: Header
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "km_hcalls.h"
#include "km.h"

/*
 * User space implementation of hypercalls.
 * These functions are called from kvm_vcpu_run() when guest makes hypercall
 * vmexit.
 *
 * km_hcalls_init() registers hypercalls in the table indexed by hcall #
 * TODO: make registration configurable so only payload specific set of hcalls
 * is registered
 */

/*
 * guest code executed exit(status);
 */
static int halt_hcall(void *ga, int *status)
{
   km_hlt_hc_t *arg = (typeof(arg))ga;
   *status = arg->exit_code;
   return 1;
}

/*
 * write a buffer to stdout
 */
static int stdout_hcall(void *ga, int *status)
{
   km_stdout_hc_t *arg = (typeof(arg))ga;
   int i, rc;

   for (i = 0; i < arg->length; i += rc) {
      if ((rc = write(STDOUT_FILENO, km_gva_to_kma(arg->data + i),
                      arg->length - i)) < 0) {
         *status = errno;
         return -1;
      }
   }
   return 0;
}

km_hcall_fn_t km_hcalls_table[KM_HC_COUNT] = {
    halt_hcall,
    stdout_hcall,
};
