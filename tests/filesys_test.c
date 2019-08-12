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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "syscall.h"

#include "greatest/greatest.h"

TEST test_close()
{
   ASSERT_EQ(-1, close(-1));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, close(1000000));
   ASSERT_EQ(EBADF, errno);
   PASS();
}

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

   /*
    * Various bad file descriptors
    */
   ASSERT_EQ(-1, fstat(fd, &st));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, fstat(-1, &st));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, fstat(1000000, &st));
   ASSERT_EQ(EBADF, errno);

   struct statx stx;
   int rc = syscall(SYS_statx, AT_FDCWD, "/", 0, STATX_ALL, &stx);
   if (rc != 0) {
      perror("statx");
   }
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, stx.stx_mode & S_IFMT);
   PASS();
}

TEST test_getdents()
{
   // from 'man 2 getdents64
   static const int ndirent = 1000;
   struct linux_dirent64 {
      ino64_t d_ino;           /* 64-bit inode number */
      off64_t d_off;           /* 64-bit offset to next structure */
      unsigned short d_reclen; /* Size of this dirent */
      unsigned char d_type;    /* File type */
      char d_name[];           /* Filename (null-terminated) */
   } dbuf[ndirent];

   int fd = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd);
   int ret = syscall(SYS_getdents64, fd, dbuf, ndirent);
   fprintf(stderr, "ret=%d errno=%d\n", ret, errno);
   ASSERT_NOT_EQ(-1, ret);
   ASSERT_EQ(0, close(fd));

   ASSERT_EQ(-1, syscall(SYS_getdents64, fd, dbuf, ndirent));
   ASSERT_EQ(EBADF, errno);
   PASS();
}

// socket pair and pipes
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

TEST test_dup()
{
   // Setup: 2 gaps
   int fd1 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd2);
   int fd3 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd3);
   int fd4 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd4);
   int fd5 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd5);
   ASSERT_EQ(0, close(fd2));
   ASSERT_EQ(0, close(fd4));

   // fcntl dup sets a starting point for new fd search.
   int ret = fcntl(fd1, F_DUPFD, fd3);
   ASSERT_EQ(fd4, ret);
   ASSERT_EQ(0, close(ret));

   // dup will take lowest
   ret = dup(fd5);
   ASSERT_EQ(fd2, ret);
   ASSERT_EQ(0, close(ret));

   // dup 2 allows choice of target
   ret = dup2(fd5, fd2);
   ASSERT_EQ(fd2, ret);
   ASSERT_EQ(0, close(ret));

   ret = dup3(fd5, fd2, O_CLOEXEC);
   ASSERT_EQ(fd2, ret);
   ASSERT_EQ(0, close(ret));

   ASSERT_EQ(0, close(fd1));
   ASSERT_EQ(0, close(fd3));
   ASSERT_EQ(0, close(fd5));

   // Try on closed fd's
   ASSERT_EQ(-1, fcntl(fd5, F_DUPFD, fd3));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, dup(fd5));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, dup2(fd5, fd2));
   ASSERT_EQ(EBADF, errno);
   ASSERT_EQ(-1, dup3(fd5, fd2, O_CLOEXEC));
   ASSERT_EQ(EBADF, errno);

   PASS();
}

/*
 * Open and close of eventfd.
 */
TEST test_eventfd()
{
   int fd = eventfd(0, 0);
   ASSERT_NOT_EQ(-1, fd);
   close(fd);
   PASS();
}

TEST test_getrlimit_nofiles()
{
   struct rlimit rlim;
   ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &rlim));
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_close);
   RUN_TEST(test_stat);
   RUN_TEST(test_getdents);
   RUN_TEST(test_socketpair);
   RUN_TEST(test_open_fd_fill);
   RUN_TEST(test_dup_fd_fill);
   RUN_TEST(test_dup);
   RUN_TEST(test_eventfd);
   RUN_TEST(test_getrlimit_nofiles);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
