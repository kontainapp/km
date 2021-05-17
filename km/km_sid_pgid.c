/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <unistd.h>
#include <sys/types.h>

#include "km.h"
#include "km_fork.h"
#include "km_hcalls.h"

/*
 * Rudimentary support for session id and process group id hypercalls.
 */

uint64_t km_setsid(km_vcpu_t* vcpu)
{
   pid_t sid = setsid();
   if (sid == -1) {
      return -errno;
   }
   return sid;
}

uint64_t km_getsid(km_vcpu_t* vcpu, pid_t pid)
{
   pid_t returned_sid;

   if ((returned_sid = getsid(pid)) < 0) {
      return -errno;
   }
   return returned_sid;
}

uint64_t km_setpgid(km_vcpu_t* vcpu, pid_t pid, pid_t pgid)
{
   int rc;
   if ((rc = setpgid(pid, pgid)) < 0) {
      return -errno;
   }
   return rc;
}

uint64_t km_getpgid(km_vcpu_t* vcpu, pid_t pid)
{
   pid_t pgid;

   if ((pgid = getpgid(pid)) < 0) {
      return -errno;
   }
   return pgid;
}
