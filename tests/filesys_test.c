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
 * Tests for file system oriented operations.
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

   int fd = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd);
   ASSERT_EQ(0, fstat(fd, &st));
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFMT);
   ASSERT_EQ(0, close(fd));
   PASS();
}

TEST test_socketpair()
{
   int fd[2];

   ASSERT_EQ(0, socketpair(PF_LOCAL, SOCK_STREAM, 0, fd));
   ASSERT_EQ(0, close(fd[0]));
   ASSERT_EQ(0, close(fd[1]));

   ASSERT_EQ(0, pipe(fd));
   ASSERT_EQ(0, close(fd[0]));
   ASSERT_EQ(0, close(fd[1]));

   ASSERT_EQ(0, pipe2(fd, O_CLOEXEC));
   ASSERT_EQ(0, close(fd[0]));
   ASSERT_EQ(0, close(fd[1]));
   PASS();
}

/*
 * Tests that lowest availble file descriptor is used for open
 */
TEST test_open_fd_fill()
{
   // open two file descriptors
   int fd1 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and open a new one
   ASSERT_EQ(0, close(fd1));
   int fd3 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   // Ensure fd1 value was re-used.
   ASSERT_EQ(fd1, fd3);
   // clean it up.
   ASSERT_EQ(0, close(fd2));
   ASSERT_EQ(0, close(fd3));

   PASS();
}

/*
 * Tests that lowest availble file descriptor is used for dup
 */
TEST test_dup_fd_fill()
{
   // open two file descriptors
   int fd1 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and dup
   ASSERT_EQ(0, close(fd1));
   int fd3 = dup(fd2);
   ASSERT_NOT_EQ(-1, fd1);
   // Ensure fd1 value was re-used.
   ASSERT_EQ(fd1, fd3);
   // clean it up.
   ASSERT_EQ(0, close(fd2));
   ASSERT_EQ(0, close(fd3));

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
   RUN_TEST(test_open_fd_fill);
   RUN_TEST(test_dup_fd_fill);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
