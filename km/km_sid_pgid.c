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

#include <sys/types.h>
#include <unistd.h>

#include "km.h"
#include "km_hcalls.h"
#include "km_fork.h"

/*
 * Rudimentary support for session id and process group id hypercalls.
 */

uint64_t km_setsid(km_vcpu_t* vcpu)
{
   pid_t sid = setsid();
   if (sid == -1) {
      return -errno;
   }
   pid_t kontain_sid = km_pid_xlate_lpid(sid);
   return kontain_sid;
}

uint64_t km_getsid(km_vcpu_t* vcpu, pid_t pid)
{
   pid_t linux_pid = pid;
   pid_t returned_sid;

   if (pid != 0) {
      linux_pid = km_pid_xlate_kpid(pid);
   }
   returned_sid = getsid(linux_pid);
   if (returned_sid == -1) {
      return -errno;
   }
   pid_t kontain_sid = km_pid_xlate_lpid(returned_sid);
   return kontain_sid;
}

uint64_t km_setpgid(km_vcpu_t* vcpu, pid_t pid, pid_t pgid)
{
   pid_t linux_pid = pid;
   pid_t linux_pgid = pgid;

   if (pid != 0) {
      linux_pid = km_pid_xlate_kpid(pid);
   }
   if (pgid != 0) {
      linux_pgid = km_pid_xlate_kpid(pgid);
   }

   int rc;
   rc = setpgid(linux_pid, linux_pgid);
   if (rc == -1) {
      return -errno;
   }
   return rc;
}

uint64_t km_getpgid(km_vcpu_t* vcpu, pid_t pid)
{
   pid_t linux_pid = pid;
   pid_t linux_pgid;

   if (pid != 0) {
      linux_pid = km_pid_xlate_kpid(pid);
   }
   linux_pgid = getpgid(linux_pid);
   if (linux_pgid == -1) {
      return -errno;
   }
   pid_t kontain_pgid = km_pid_xlate_lpid(linux_pgid);
   return kontain_pgid;
}
