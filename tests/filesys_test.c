/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
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
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>

#ifndef STATX_ALL
#include <linux/stat.h>
#endif

#include "syscall.h"

#include "greatest/greatest.h"

int ac;
char** av;

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
   ASSERT_NEQ(-1, fd);
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

   fd = open("/proc/self", O_RDONLY);
   rc = syscall(SYS_statx, fd, "exe", 0, STATX_ALL, &stx);
   ASSERT_EQ_FMT(0, rc, "%d");
   close(fd);

   fd = open("/proc/self/exe", O_RDONLY);
   rc = syscall(SYS_statx, fd, "..", 0, STATX_ALL, &stx);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ_FMT(ENOTDIR, errno, "%d");
   rc = syscall(SYS_fchown, fd, 777, 777);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ_FMT(EPERM, errno, "%d");
   rc = syscall(SYS_fchmod, fd, 0444);
   ASSERT_EQ(0, rc);
   rc = syscall(SYS_chown, "/proc/self/exe", 777, 777);
   ASSERT_EQ_FMT(-1, rc, "%d");
   ASSERT_EQ_FMT(EPERM, errno, "%d");
   rc = syscall(SYS_lchown, "/proc/self/exe", 777, 777);
   ASSERT_EQ_FMT(-1, rc, "%d");
   ASSERT_EQ_FMT(EPERM, errno, "%d");
   rc = syscall(SYS_chmod, "/proc/self/exe", 0333);
   ASSERT_EQ(0, rc);
   rc = syscall(SYS_chmod, "/proc/self/exe", 0775);   // restore sanity

   close(fd);

   PASS();
}

TEST test_getdents()
{
   // from 'man 2 getdents
   static const int bufsize = 24 * 4;
   struct linux_dirent {
      unsigned long d_ino;     /* Inode number */
      unsigned long d_off;     /* Offset to next linux_dirent */
      unsigned short d_reclen; /* Length of this linux_dirent */
      char d_name[];           /* Filename (null-terminated) */
                               /* length is actually (d_reclen - 2 -
                                  offsetof(struct linux_dirent, d_name)) */
      /*
      char           pad;       // Zero padding byte
      char           d_type;    // File type (only since Linux
                                // 2.6.4); offset is (d_reclen - 1)
      */
   };
   char dbuf[bufsize];

   int fd = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   int rc = syscall(SYS_getdents, fd, dbuf, bufsize);
   ASSERT_NEQ(-1, rc);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   rc = syscall(SYS_getdents, fd, dbuf, bufsize);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);

   fd = open("/proc/self/fd", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   while ((rc = syscall(SYS_getdents, fd, dbuf, bufsize)) > 0) {
      if (greatest_get_verbosity() > 0) {
         printf("----- getdents %d\n", rc);
      }
      struct linux_dirent* entry;
      for (off64_t offset = 0; offset < rc; offset += entry->d_reclen) {
         entry = (struct linux_dirent*)(dbuf + offset);
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
         }
         if (greatest_get_verbosity() > 0) {
            char tmp[256] = "/proc/self/fd/";
            strcat(tmp, entry->d_name);
            char buf[256];
            int idx = readlink(tmp, buf, sizeof(buf));
            buf[idx] = 0;
            printf("name <%s> is <%s>\n", tmp, buf);
         }
         ino64_t ino = atol(entry->d_name);
         ASSERT(0 <= ino && ino <= 6);
      }
   }
   ASSERT_NEQ(-1, rc);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   PASS();
}

TEST test_getdents64()
{
   // from 'man 2 getdents64
   static const int bufsize = 24 * 4;
   struct linux_dirent64 {
      ino64_t d_ino;           /* 64-bit inode number */
      off64_t d_off;           /* 64-bit offset to next structure */
      unsigned short d_reclen; /* Size of this dirent */
      unsigned char d_type;    /* File type */
      char d_name[];           /* Filename (null-terminated) */
   };
   char dbuf[bufsize];

   int fd = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   int rc = syscall(SYS_getdents64, fd, dbuf, bufsize);
   ASSERT_NEQ(-1, rc);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   rc = syscall(SYS_getdents64, fd, dbuf, bufsize);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);

   fd = open("/proc/self/fd", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   while ((rc = syscall(SYS_getdents64, fd, dbuf, bufsize)) > 0) {
      if (greatest_get_verbosity() > 0) {
         printf("----- getdents64 %d\n", rc);
      }
      struct linux_dirent64* entry;
      for (off64_t offset = 0; offset < rc; offset += entry->d_reclen) {
         entry = (struct linux_dirent64*)(dbuf + offset);
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
         }
         if (greatest_get_verbosity() > 0) {
            char tmp[256] = "/proc/self/fd/";
            strcat(tmp, entry->d_name);
            char buf[256];
            int idx = readlink(tmp, buf, sizeof(buf));
            buf[idx] = 0;
            printf("name <%s> is <%s>\n", tmp, buf);
         }
         ino64_t ino = atol(entry->d_name);
         ASSERT(0 <= ino && ino <= 6);
      }
   }
   ASSERT_NEQ(-1, rc);
   rc = close(fd);
   ASSERT_EQ(0, rc);

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
 * Tests that lowest available file descriptor is used for open
 */
TEST test_open_fd_fill()
{
   int rc;
   // open two file descriptors
   int fd1 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and open a new one
   rc = close(fd1);
   ASSERT_EQ(0, rc);
   int fd3 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd3);
   // Ensure fd1 value was re-used.
   ASSERT_EQ(fd1, fd3);
   // clean it up.
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(fd3);
   ASSERT_EQ(0, rc);

   PASS();
}

TEST test_openat()
{
   int fd;
   struct stat st1, st2;

   fd = openat(-1, "/", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   fstat(fd, &st1);
   stat("/", &st2);
   ASSERT_EQ(st1.st_ino, st2.st_ino);
   close(fd);

   fd = openat(AT_FDCWD, "Makefile", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   fstat(fd, &st1);
   stat("Makefile", &st2);
   ASSERT_EQ(st1.st_ino, st2.st_ino);
   close(fd);

   int fd1 = open("..", O_RDONLY);
   fd = openat(fd1, "tests", O_RDONLY);
   ASSERT_NEQ(-1, fd);
   fstat(fd, &st1);
   stat("../tests", &st2);
   ASSERT_EQ(st1.st_ino, st2.st_ino);
   close(fd);

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
   ASSERT_NEQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd2);
   // Ensure 1st one has lower fd value than second
   ASSERT(fd1 < fd2);
   // Close the first one and dup
   rc = close(fd1);
   ASSERT_EQ(0, rc);
   int fd3 = dup(fd2);
   ASSERT_NEQ(-1, fd1);
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
   ASSERT_NEQ(-1, fd1);
   int fd2 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd2);
   int fd3 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd3);
   int fd4 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd4);
   int fd5 = open("/", O_RDONLY);
   ASSERT_NEQ(-1, fd5);
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(fd4);
   ASSERT_EQ(0, rc);

   // fcntl dup sets a starting point for new fd search.
   int ret = fcntl(fd1, F_DUPFD, fd3);
   ASSERT_EQ_FMT(fd4, ret, "%d");
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

   /*
    * Interestingly, this is a place where
    * the musl implementation doesn't strictly
    * match the man page. In particular, the
    * man page says EINVAL is returned when
    * "flags contain an invalid value". That's not
    * actually what MUSL does. If O_CLOEXEC is
    * not set, then MUSL unconditionally calls
    * dup2, ignoring potential garbage in flags.
    */

   rc = dup3(fd5, fd2, 0xffff);
   ASSERT_NEQ(-1, rc);
   close(rc);

   rc = dup3(fd5, fd2, 0xffff | O_CLOEXEC);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EINVAL, errno);

   /*
    * dup to self. dup2 and dup3 behave slightly differently.
    */
   rc = dup2(fd5, fd5);
   ASSERT_EQ(fd5, rc);

   rc = dup3(fd5, fd5, 0);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EINVAL, errno);

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
   ASSERT_NEQ(-1, fd);
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

TEST test_proc_fd()
{
   char* fname = ".";
   int fd = open(fname, O_RDONLY);
   ASSERT_NEQ(-1, fd);
   char procname[128];
   snprintf(procname, sizeof(procname), "/proc/self/fd/%d", fd);
   char* realname = realpath(fname, NULL);

   char slink[128];

   ASSERT_NEQ(-1, readlink(procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   ASSERT_NEQ(-1, readlinkat(fd, procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   ASSERT_NEQ(-1, readlinkat(AT_FDCWD, procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   snprintf(procname, sizeof(procname), "/proc/%d/fd/%d", getpid(), fd);

   ASSERT_NEQ(-1, readlink(procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   ASSERT_NEQ(-1, readlinkat(fd, procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   ASSERT_NEQ(-1, readlinkat(AT_FDCWD, procname, slink, sizeof(slink)));
   fprintf(stderr, "slink=%s\n", slink);
   ASSERT_EQ(0, strcmp(slink, realname));

   close(fd);
   PASS();
}

TEST test_proc_sched()
{
   char procname[128], buf_self[4096], buf_pid[4096];
   snprintf(procname, sizeof(procname), "/proc/%d/sched", getpid());

   int self_fd = open("/proc/self/sched", O_RDONLY);
   ASSERT_NEQ(-1, self_fd);

   int pid_fd = open(procname, O_RDONLY);
   ASSERT_NEQ(-1, pid_fd);

   int self_rc = read(self_fd, buf_self, sizeof(buf_self));
   ASSERT_NEQ(-1, self_rc);
   int pid_rc = read(pid_fd, buf_pid, sizeof(buf_pid));
   ASSERT_NEQ(-1, pid_rc);

   ASSERT_EQ(self_rc, pid_rc);
   strtok(buf_pid, "\n");
   strtok(buf_self, "\n");
   ASSERT_EQ(0, strcmp(buf_pid, buf_self));
   printf("%s\n", buf_self);

   close(self_fd);
   close(pid_fd);
   PASS();
}

TEST test_proc_cmdline()
{
   char procname[128], buf_self[4096], buf_pid[4096], buf[4096];
   snprintf(procname, sizeof(procname), "/proc/%d/cmdline", getpid());

   char* cp = buf;
   int sz = sizeof(buf);
   for (int i = 0; i < ac; i++) {
      int cnt = snprintf(cp, sz, "%s", av[i]) + 1;
      cp += cnt;
      sz -= cnt;
   }

   int self_fd = open("/proc/self/cmdline", O_RDONLY);
   ASSERT_NEQ(-1, self_fd);

   int pid_fd = open(procname, O_RDONLY);
   ASSERT_NEQ(-1, pid_fd);

   int self_rc = read(self_fd, buf_self, sizeof(buf_self));
   ASSERT_NEQ(-1, self_rc);
   int pid_rc = read(pid_fd, buf_pid, sizeof(buf_pid));
   ASSERT_NEQ(-1, pid_rc);
   ASSERT_EQ(self_rc, pid_rc);
   ASSERT_EQ(0, memcmp(buf_pid, buf_self, self_rc));
   ASSERT_EQ(0, memcmp(buf, buf_self, self_rc));

   close(self_fd);
   close(pid_fd);
   PASS();
}

TEST test_close_stdio()
{
   struct stat st;
   int rc = fstat(0, &st);
   ASSERT_EQ(0, rc);
   close(0);
   rc = fstat(0, &st);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EBADF, errno);

   PASS();
}

TEST test_statfs()
{
   struct statfs stf;
   ASSERT_EQ(0, statfs("/", &stf));
   ASSERT_EQ(0, fstatfs(0, &stf));
   PASS();
}

// pselect is a libc front end to pselect6.
TEST test_pselect6()
{
   fd_set rfds;
   fd_set wfds;
   fd_set efds;

   FD_ZERO(&rfds);
   FD_ZERO(&wfds);
   FD_ZERO(&efds);

   FD_SET(0, &rfds);
   FD_SET(0, &wfds);
   FD_SET(0, &efds);

   struct timespec ts = {};

   ASSERT_EQ(-1, syscall(SYS_pselect6, 1, &rfds, &wfds, &efds, &ts, (void*)1));
   ASSERT_EQ(errno, EFAULT);

   uint64_t sigs[] = {1, 1};
   ASSERT_EQ(-1, syscall(SYS_pselect6, 1, &rfds, &wfds, &efds, &ts, sigs));
   ASSERT_EQ(errno, EFAULT);
   PASS();
}

int got_sigio = 0;
void handle_sigio(int signo, siginfo_t *info, void *stuff)
{
   got_sigio = 1;
}

TEST test_fcntl_fsetown()
{
   got_sigio = 0;

   // open test file
   int pipefd[2];    // pipefd[0] is the read end
   ASSERT_NEQ(-1, pipe(pipefd));

   // set O_ASYNC on the read end of the pipe
   ASSERT_NEQ(-1, fcntl(pipefd[0], F_SETFL, O_ASYNC));

   // add signal handler for SIGIO
   struct sigaction act;
   act.sa_sigaction = handle_sigio;
   act.sa_flags = SA_SIGINFO;
   sigfillset(&act.sa_mask);
   ASSERT_EQ(0, sigaction(SIGIO, &act, NULL));

   // use fcntl(F_SETOWN, ) to cause SIGIO when we write on the test file
   ASSERT_NEQ(-1, fcntl(pipefd[0], F_SETOWN, getpid()));

   // write into write end of the pipe
   int bc = write(pipefd[1], "noise", strlen("noise"));
   ASSERT_EQ(bc, strlen("noise"));

   // pause to allow time for signal handler to run?
   int i;
   for (i = 0; i < 10; i++) {
      if (got_sigio != 0) {
         break;
      }
      struct timespec snooze = {0, 50000000};  // 50ms
      if (nanosleep(&snooze, NULL) < 0) {
         ASSERT_EQ(EINTR, errno);
      }
   }
   fprintf(stdout, "%d spins waiting for SIGIO\n", i);

   // verify that signal handler was entered
   ASSERT_NEQ(0, got_sigio);

   // close test file
   ASSERT_EQ(0, close(pipefd[0]));
   ASSERT_EQ(0, close(pipefd[1]));

   // remove our signal handler
   act.sa_handler = SIG_IGN;
   act.sa_flags = 0;
   ASSERT_EQ(0, sigaction(SIGIO, &act, NULL));

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   ac = argc;
   av = argv;

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_statfs);
   RUN_TEST(test_close);
   RUN_TEST(test_stat);
   RUN_TEST(test_getdents);
   RUN_TEST(test_getdents64);
   RUN_TEST(test_socketpair);
   RUN_TEST(test_openat);
   RUN_TEST(test_open_fd_fill);
   RUN_TEST(test_dup_fd_fill);
   RUN_TEST(test_dup);
   RUN_TEST(test_eventfd);
   RUN_TEST(test_getrlimit_nofiles);
   RUN_TEST(test_bad_fd);
   RUN_TEST(test_proc_fd);
   RUN_TEST(test_proc_sched);
   RUN_TEST(test_proc_cmdline);
   RUN_TEST(test_close_stdio);
   RUN_TEST(test_pselect6);
   RUN_TEST(test_fcntl_fsetown);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
