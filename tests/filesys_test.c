/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Simple test for signal system call.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "greatest/greatest.h"

TEST test_stat()
{
   struct stat st;

   ASSERT_EQ(0, stat("/", &st));
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFMT);
   PASS();
}

TEST test_socketpair()
{
   int fd[2];

   ASSERT_EQ(0, socketpair(PF_LOCAL, SOCK_STREAM, 0, fd));
   ASSERT_EQ(0, close(fd[0]));
   ASSERT_EQ(0, close(fd[1]));
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_stat);
   RUN_TEST(test_socketpair);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
