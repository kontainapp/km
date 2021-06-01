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

/*
 * Simple session id and process group id hypercall tests.
 */

#include <errno.h>
#include <error.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "greatest/greatest.h"
#include "test_common_functions.h"

TEST sid_test(void)
{
   pid_t initial_sid;
   pid_t initial_pgid;
   pid_t sid;

   // getpgid() for this process
   initial_pgid = getpgid(0);
   ASSERT_NEQ(-1, initial_pgid);
   fprintf(stdout, "pid %d's initial pgid %d\n", getpid(), initial_pgid);

   // getsid() for this process
   initial_sid = getsid(0);
   ASSERT_NEQ(-1, initial_sid);
   fprintf(stdout, "pid %d's initial sid is %d\n", getpid(), initial_sid);

   // getsid() on pid returned by getpid()
   sid = getsid(getpid());
   ASSERT_NEQ(-1, sid);
   fprintf(stdout, "getpid's sid is %d\n", sid);

   // Get an invalid pid.
   pid_t invalid_pid = get_pid_max();
   ASSERT_NEQ(-1, invalid_pid);

   // getsid() for invalid pid
   sid = getsid(invalid_pid);
   fprintf(stdout, "getsid(%d) returns %d, errno %d\n", invalid_pid, sid, errno);
   ASSERT_EQ(-1, sid);
   ASSERT_EQ(ESRCH, errno);

   // Push any buffered output so the child doesn't flush any of the parents buffered output.
   fflush(stdout);

   // To test setsid() success we need to fork() to ensure setsid() will succeed.
   pid_t child = fork();
   if (child == 0) {
      // We are in the child, we can't use the greatest macros here.
      sid = setsid();
      fprintf(stdout, "new session id %d, errno %d\n", sid, errno);
      exit(sid == -1 ? 1 : 0);
   } else {
      int wstatus;
      pid_t finished = wait(&wstatus);
      fprintf(stdout,
              "wait() returned, finished %d, errno %d, child %d, wstatus 0x%x\n",
              finished,
              errno,
              child,
              wstatus);
      ASSERT_NEQ(-1, finished);
      ASSERT_EQ(child, finished);
      ASSERT_EQ(0, WEXITSTATUS(wstatus));
   }

   PASS();
}

TEST pgid_test(void)
{
   pid_t pgid;

   // Get this process' pgid
   pgid = getpgid(0);
   ASSERT_NEQ(-1, pgid);
   fprintf(stdout, "current process pgid is %d\n", pgid);

   // Get the pgid of the process id returned by getpid()
   pgid = getpgid(getpid());
   ASSERT_NEQ(-1, pgid);
   fprintf(stdout, "getpid()'s pgid is %d\n", pgid);

   // get pgid using an invalid process id
   pgid = getpgid(7777);
   ASSERT_EQ(-1, pgid);
   ASSERT_EQ(ESRCH, errno);

   int rv;

   // Use an invalid pgid
   rv = setpgid(-3333, 0);
   ASSERT_EQ(-1, rv);
   ASSERT_EQ(EINVAL, errno);

   // make this process a process a member of its own process group
   rv = setpgid(0, 0);
   ASSERT_EQ(0, rv);

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(sid_test);
   RUN_TEST(pgid_test);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
