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
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifndef STATX_ALL
#include <linux/stat.h>
#endif

#include "syscall.h"

#include "greatest/greatest.h"

TEST test_close()
{
   int rc = close(-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = close(1000000);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   PASS();
}

TEST test_stat()
{
   struct stat st;
   int rc;

   rc = stat("/", &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFMT);

   int fd = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd);
   rc = fstat(fd, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFMT);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   /*
    * Various bad file descriptors
    */
   rc = fstat(fd, &st);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = fstat(-1, &st);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = fstat(1000000, &st);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);

   struct statx stx;
   rc = syscall(SYS_statx, AT_FDCWD, "/", 0, STATX_ALL, &stx);
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
   int rc = syscall(SYS_getdents64, fd, dbuf, ndirent);
   ASSERT_NOT_EQ(-1, rc);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   rc = syscall(SYS_getdents64, fd, dbuf, ndirent);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   PASS();
}

// socket pair and pipes
TEST test_socketpair()
{
   int fd[2];
   int rc;

   rc = socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);
   ASSERT_EQ(0, rc);
   rc = close(fd[0]);
   ASSERT_EQ(0, rc);
   rc = close(fd[1]);
   ASSERT_EQ(0, rc);

   rc = pipe(fd);
   ASSERT_EQ(0, rc);
   rc = close(fd[0]);
   ASSERT_EQ(0, rc);
   rc = close(fd[1]);
   ASSERT_EQ(0, rc);

   rc = pipe2(fd, O_CLOEXEC);
   ASSERT_EQ(0, rc);
   rc = close(fd[0]);
   ASSERT_EQ(0, rc);
   rc = close(fd[1]);
   ASSERT_EQ(0, rc);
   PASS();
}

/*
 * Tests that lowest availble file descriptor is used for open
 */
TEST test_open_fd_fill()
{
   int rc;
   // open two file descriptors
   int fd1 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and open a new one
   rc = close(fd1);
   ASSERT_EQ(0, rc);
   int fd3 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd3);
   // Ensure fd1 value was re-used.
   ASSERT_EQ(fd1, fd3);
   // clean it up.
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(fd3);
   ASSERT_EQ(0, rc);

   PASS();
}

/*
 * Tests that lowest availble file descriptor is used for dup
 */
TEST test_dup_fd_fill()
{
   int rc;
   // open two file descriptors
   int fd1 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NOT_EQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and dup
   rc = close(fd1);
   ASSERT_EQ(0, rc);
   int fd3 = dup(fd2);
   ASSERT_NOT_EQ(-1, fd1);
   // Ensure fd1 value was re-used.
   ASSERT_EQ(fd1, fd3);
   // clean it up.
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(fd3);
   ASSERT_EQ(0, rc);

   PASS();
}

TEST test_dup()
{
   int rc;
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
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(fd4);
   ASSERT_EQ(0, rc);

   // fcntl dup sets a starting point for new fd search.
   int ret = fcntl(fd1, F_DUPFD, fd3);
   ASSERT_EQ(fd4, ret);
   rc = close(ret);
   ASSERT_EQ(0, rc);
   ret = fcntl(fd1, F_DUPFD_CLOEXEC, fd3);
   ASSERT_EQ(fd4, ret);
   rc = close(ret);
   ASSERT_EQ(0, rc);

   // dup will take lowest
   ret = dup(fd5);
   ASSERT_EQ(fd2, ret);
   rc = close(ret);
   ASSERT_EQ(0, rc);

   // dup 2 allows choice of target
   ret = dup2(fd5, fd2);
   ASSERT_EQ(fd2, ret);
   rc = close(ret);
   ASSERT_EQ(0, rc);

   ret = dup3(fd5, fd2, O_CLOEXEC);
   ASSERT_EQ(fd2, ret);
   rc = close(ret);
   ASSERT_EQ(0, rc);

   rc = close(fd1);
   ASSERT_EQ(0, rc);
   rc = close(fd3);
   ASSERT_EQ(0, rc);
   rc = close(fd5);
   ASSERT_EQ(0, rc);

   // Try on closed fd's
   rc = fcntl(fd5, F_DUPFD, fd3);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = dup(fd5);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = dup2(fd5, fd2);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   rc = dup3(fd5, fd2, O_CLOEXEC);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);

   PASS();
}

/*
 * Open and close of eventfd.
 */
TEST test_eventfd()
{
   int rc;
   int fd = eventfd(0, 0);
   ASSERT_NOT_EQ(-1, fd);
   rc = close(fd);
   ASSERT_EQ(0, rc);
   PASS();
}

TEST test_getrlimit_nofiles()
{
   int rc;
   struct rlimit rlim;
   rc = getrlimit(RLIMIT_NOFILE, &rlim);
   ASSERT_EQ(0, rc);
   PASS();
}

/*
 * feeds system calls that take file descriptors a set of bad file descriptors
 */
int badfd[] = {-1, 1000000, 2000, 0};
TEST test_bad_fd()
{
   int rc;

   // close
   for (int i = 0; badfd[i] != 0; i++) {
      rc = close(badfd[i]);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // shutdown
   for (int i = 0; badfd[i] != 0; i++) {
      rc = shutdown(badfd[i], 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // ioctl
   for (int i = 0; badfd[i] != 0; i++) {
      rc = ioctl(badfd[i], 0, 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // fcntl
   for (int i = 0; badfd[i] != 0; i++) {
      rc = fcntl(badfd[i], 0, 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // lseek
   for (int i = 0; badfd[i] != 0; i++) {
      rc = lseek(badfd[i], 0, 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // fchdir
   for (int i = 0; badfd[i] != 0; i++) {
      rc = fchdir(badfd[i]);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // fstat
   for (int i = 0; badfd[i] != 0; i++) {
      struct stat st;
      rc = fstat(badfd[i], &st);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // dup
   for (int i = 0; badfd[i] != 0; i++) {
      rc = dup(badfd[i]);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // dup2
   for (int i = 0; badfd[i] != 0; i++) {
      rc = dup2(badfd[i], 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   rc = dup2(0, -1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   // dup3
   for (int i = 0; badfd[i] != 0; i++) {
      rc = dup3(badfd[i], 0, 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   rc = dup3(0, -1, 0);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);
   // read
   for (int i = 0; badfd[i] != 0; i++) {
      char buf[128];
      rc = read(badfd[i], buf, sizeof(buf));
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // readv
   struct iovec iov = {.iov_base = (void*)-1, .iov_len = 1281};
   for (int i = 0; badfd[i] != 0; i++) {
      rc = readv(badfd[i], &iov, 1);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
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
   RUN_TEST(test_bad_fd);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
