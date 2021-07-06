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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "greatest/greatest.h"

/*
 * A simple test to exercise the getgroups() system call.
 * We could expand to test other id related system calls as the need arises.
 */

TEST getXXid(void)
{
   uid_t uid;
   gid_t gid;

   /*
    * Try to test the getuid() family of functions.
    * Expecting that root is not running the program may be unrealistic.
    * Recall that km formerly returned zero for calls to these functions.
    */

   uid = geteuid();
   ASSERT_NEQ(uid, -1);
   ASSERT_NEQ(uid, 0);
   uid = getuid();
   ASSERT_NEQ(uid, -1);
   ASSERT_NEQ(uid, 0);
   gid = getegid();
   ASSERT_NEQ(gid, -1);
   ASSERT_NEQ(gid, 0);
   gid = getgid();
   ASSERT_NEQ(gid, -1);
   ASSERT_NEQ(gid, 0);

   PASS();
}

TEST getgroups_smoke(void)
{
   int ngroups;
   gid_t grouplist[NGROUPS_MAX];

   // If there are no groups the passed address is never used.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
   ngroups = getgroups(NGROUPS_MAX, NULL);
#pragma GCC diagnostic pop
   ASSERT(ngroups == -1 || ngroups == 0);
   if (ngroups < 0) {
      ASSERT_EQ(errno, EFAULT);
   }

   // Returns the number of groups, but group list is not returned
   ngroups = getgroups(0, grouplist);
   ASSERT_NEQ(ngroups, -1);

   if (ngroups > 1) {
      ngroups = getgroups(ngroups - 1, grouplist);
      ASSERT_EQ(ngroups, -1);
      ASSERT_EQ(errno, EINVAL);
   }

   PASS();
}

TEST getgroups_print(void)
{
   int ngroups;
   gid_t grouplist[NGROUPS_MAX];

   ngroups = getgroups(NGROUPS_MAX, grouplist);
   ASSERT_NEQ(ngroups, -1);
   /*
    * Print something to compare with output from:
    *   id -G | tr " " "\n" | sort -n
    */
   for (int i = 0; i < ngroups; i++) {
      fprintf(stdout, "%d\n", grouplist[i]);
   }
   if (ngroups == 0) {
      // To be like "id -G" we print the gid returned by getgid() if getgroups() returns nothing.
      grouplist[0] = getgid();
      fprintf(stdout, "%d\n", grouplist[0]);
   }

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(getgroups_smoke);
   RUN_TEST(getgroups_print);
   RUN_TEST(getXXid);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
