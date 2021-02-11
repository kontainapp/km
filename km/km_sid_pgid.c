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
 *
 * km currently does nothing with session id's and process group id's.
 * It propagates arguments to the session id and process group id hypercalls
 * to the kernel system calls and returns the values returned by the kernel.
 * session id's and process group id's are derived directly from process id's.
 * Since km currently maintains its own simple pid namespace that the
 * linux kernel knows nothing about, km translates km pids into linux pid's
 * before passing them on to the kernel as arguments to the sid and pgid
 * hypercalls.  These kernel system calls may return linux session id's and
 * process group id's which are essentially pid's that have been made into these id's.
 * Since pids, sid's, and pgid's are related, km translates returned linux sid's and pgid's
 * into km sid's and pgid's using the pid xlate tables it keeps and returns
 * the xlated values from the hypercalls to the payload so it is none the wiser.
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
