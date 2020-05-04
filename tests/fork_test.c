/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
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
 * Simple test of the fork, execve, execveat (really fexecve), wait4, waitid,
 * and pipe system calls.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sched.h>

#define HELLO_WORLD_TEST "hello_test.kmd"

static char* args[] = { HELLO_WORLD_TEST, "one", "two", "three", "four", NULL };
static char* env[] = { "ONE=one", "TWO=two", "THREE=three", "FOUR=four", "FIVE=five", NULL };
static char cwd[MAXPATHLEN];
#define READ_FD 0
#define WRITE_FD 1
int parent_write_end[2];   // [0] = read, [1] = write
int parent_read_end[2];

/*
 * Test fork() and execveat().
 * Returns:
 *   0 - success
 * != 0 - failed
 */
static int fork_exec_1(void)
{
   pid_t pid;
   int rc;

   // Output delimiter
   fprintf(stderr, "\nTest: %s\n", __FUNCTION__);

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "%s: fork failed, %s\n", __FUNCTION__, strerror(errno));
      return 1;
   }
   if (pid != 0) { // parent process
      fprintf(stderr, "Child process pid %d\n", pid);
      int wstatus = 0;
      rc = wait4(pid, &wstatus, 0, NULL);
      fprintf(stderr, "wait4() for pid %d returned %d, errno %d, wstatus 0x%x\n", pid, rc, errno, wstatus);
      if (rc < 0) {
         fprintf(stderr, "%s: wait4() for pid %d failed, %s\n", __FUNCTION__, pid, strerror(errno));
         return 1;
      }
      if (rc != pid) {
         fprintf(stderr, "%s: wait4() returned wrong pid, expected %d, got %d\n", __FUNCTION__, pid, rc);
         return 1;
      }
      // Did the child part of the test work?
      if (!WIFEXITED(wstatus)) {
         fprintf(stderr, "%s: child process exited abnormally, wstatus 0x%x\n", __FUNCTION__, wstatus);
         return 1;
      }
      if (WEXITSTATUS(wstatus) != 0) {
         fprintf(stderr, "%s: child process exited with non-zero status %d\n", __FUNCTION__, WEXITSTATUS(wstatus));
         return WEXITSTATUS(wstatus);
      }
   } else {  // the child process
#if 0
      /*
       * TODO
       * We are trying to test execveat() but musl doesn't seem to have the execveat() syscall wrapper.
       * When execveat() does exist we should test with it since the interface offers more options that
       * need to be tested.
       */
      rc = execveat(AT_FDCWD, HELLO_WORLD_TEST, args, env, AT_SYMLINK_NOFOLLOW);
      if (rc < 0) {
         fprintf(stderr, "%s: execveat() failed, %s\n", __FUNCTION__, strerror(errno));
         return 1;
      }
#else
      // We test with fexecve() if the syscall wrapper is missing
      int exefd = open(HELLO_WORLD_TEST, O_RDONLY);
      if (exefd < 0) {
         fprintf(stderr, "%s: open %s failed, %s\n", __FUNCTION__, HELLO_WORLD_TEST, strerror(errno));
         return 1;
      }
      rc = fexecve(exefd, args, env);
      if (rc < 0) {   // fexecve() does not return if successful.
         fprintf(stderr, "%s: execveat() failed, %s\n", __FUNCTION__, strerror(errno));
         close(exefd);
         return 1;
      }
#endif
   }
   return 0;
}

/*
 * Test fork() and execve().
 * Returns:
 *   0 - success
 *   != 0 - failure
 */
static int fork_exec_0(void)
{
   pid_t pid;
   char buf[512];

   // Output delimiter
   fprintf(stderr, "\nTest: %s\n", __FUNCTION__);

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "%s: Fork failed, errno %d\n", __FUNCTION__, errno);
      return 1;
   } else if (pid == 0) {
      fprintf(stderr, "%s: After fork in the child process, getpid() returns %d\n", __FUNCTION__, getpid());

      // Try out the pipe
      if (write(parent_read_end[WRITE_FD], "message from the child", strlen("message from the child")) < 0) {
         fprintf(stderr, "Child couldn't write to pipe, %s\n", strerror(errno));
      }
      if (read(parent_write_end[READ_FD], buf, sizeof(buf)) < 0) {
         fprintf(stderr, "Child couldn't read from pipe, %s\n", strerror(errno));
      } else {
         fprintf(stdout, "Child read: <%s> from parent\n", buf);
      }

      // Try out execve()
      execve(HELLO_WORLD_TEST, args, env);
      fprintf(stderr, "%s: execve() failed %s\n", __FUNCTION__, strerror(errno));
      return 1;
   } else {
      printf("After fork in the parent, child pid %d\n", pid);
      assert(pid > 0);
      // Try out the pipe
      if (write(parent_write_end[WRITE_FD], "message from the parent", strlen("message from the parent")) < 0) {
         fprintf(stderr, "Parent couldn't write to pipe, %s\n", strerror(errno));
      }
      if (read(parent_read_end[READ_FD], buf, sizeof(buf)) < 0) {
         fprintf(stderr, "Parent couldn't read from pipe, %s\n", strerror(errno));
      } else {
         fprintf(stdout, "Parent read: <%s> from child\n", buf);
      }

      // Wait for the child to finish.
      siginfo_t siginfo;
      memset(&siginfo, 0, sizeof(siginfo));
      int rc = waitid(P_PID, pid, &siginfo, WEXITED);
      fprintf(stderr, "waitid() returned %d, errno %d, siginfo.si_pid %d\n", rc, errno, siginfo.si_pid);
      if (rc < 0) {
         fprintf(stderr, "%s: waitpid() for pid %d failed, %s\n", __FUNCTION__, pid, strerror(errno));
         return 1;
      }
      if (siginfo.si_pid != pid) {
         fprintf(stderr, "%s: waitpid() returned wrong pid, expected %d, got %d\n", __FUNCTION__, pid, siginfo.si_pid);
         return 1;
      }
   }
   return 0;
}

int clone_exec_0(void)
{
   // Output delimiter
   fprintf(stderr, "\nTest: %s\n", __FUNCTION__);

   pid_t pid;
   int* ctid;
   pid = syscall(SYS_clone,
                 CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID | SIGCHLD,   // flags
                 NULL,                                                  // child_stack
                 NULL,                                                  // ptid
                 &ctid,                                                 // ctid
                 NULL);                                                 // newtls
   if (pid < 0) {
      fprintf(stderr, "%s: clone process failed, errno %d\n", __FUNCTION__, errno);
      return 1;
   } else if (pid == 0) {
      // Try out execve()
      execve(HELLO_WORLD_TEST, args, env);
      fprintf(stderr, "%s: execve() failed %s\n", __FUNCTION__, strerror(errno));
   } else {
      // Wait for the child to finish.
      siginfo_t siginfo;
      memset(&siginfo, 0, sizeof(siginfo));
      int rc = waitid(P_PID, pid, &siginfo, WEXITED);
      fprintf(stderr, "waitid() returned %d, errno %d, siginfo.si_pid %d\n", rc, errno, siginfo.si_pid);
      if (rc < 0) {
         fprintf(stderr, "%s: waitpid() for pid %d failed, %s\n", __FUNCTION__, pid, strerror(errno));
         return 1;
      }
      if (siginfo.si_pid != pid) {
         fprintf(stderr, "%s: waitpid() returned wrong pid, expected %d, got %d\n", __FUNCTION__, pid, siginfo.si_pid);
         return 1;
      }
   }
   return 0;
}

int main(int argc, char* argv[])
{
   int rv = 0;
   int rvtmp;
   struct stat statb;

   if (getcwd(cwd, sizeof(cwd)) == NULL) {
      fprintf(stderr, "getcwd() failed\n");
      return 1;
   }
   if (stat(HELLO_WORLD_TEST, &statb) != 0) {  // Can we find hello_world?
      fprintf(stderr, "stat() on %s/%s failed, %s\n", cwd, HELLO_WORLD_TEST, strerror(errno));
      return 1;
   }

   if (pipe(parent_write_end) < 0) {
      fprintf(stderr, "Couldn't create write pipe, %s\n", strerror(errno));
      return 1;
   }
   if (pipe(parent_read_end) < 0) {
      fprintf(stderr, "Couldn't create read pipe, %s\n", strerror(errno));
      return 1;
   }

   // Perform all tests but preserve any failure notification to ensure the test fails.

   rvtmp = fork_exec_0();
   if (rv == 0) {
      rv = rvtmp;
   }

   rvtmp = fork_exec_1();
   if (rv == 0) {
      rv = rvtmp;
   }

   rvtmp = clone_exec_0();
   if (rv == 0) {
      rv = rvtmp;
   }

   close(parent_write_end[0]);
   close(parent_write_end[1]);
   close(parent_read_end[0]);
   close(parent_read_end[1]);

   return rv;
}
