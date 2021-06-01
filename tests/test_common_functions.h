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

// Functions that may be useful for more than one test in the bats tests.

/*
 * Return the limit on the size of the process id.
 * We should never see a pid with the returned value or larger.
 * If there is a failure -1 is returned.
 */
static inline pid_t get_pid_max(void)
{
   FILE* pidmaxfile = fopen("/proc/sys/kernel/pid_max", "r");
   if (pidmaxfile != NULL) {
      pid_t pidmax;
      if (fscanf(pidmaxfile, "%d", &pidmax) != 1) {
         pidmax = -1;
      }
      fclose(pidmaxfile);
      return pidmax;
   }
   return -1;
}
