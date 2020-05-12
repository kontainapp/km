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
 * Simple test of the fork, execve, execveat (really fexecve), wait4, waitid, kill,
 * and pipe system calls.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#define HELLO_WORLD_TEST "hello_test.kmd"

static char* args[] = {HELLO_WORLD_TEST, "one", "two", "three", "four", NULL};
static char* env[] = {"ONE=one", "TWO=two", "THREE=three", "FOUR=four", "FIVE=five", NULL};
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
   if (pid != 0) {   // parent process
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
         fprintf(stderr,
                 "%s: child process exited with non-zero status %d\n",
                 __FUNCTION__,
                 WEXITSTATUS(wstatus));
         return WEXITSTATUS(wstatus);
      }
   } else {   // the child process
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
      fprintf(stderr,
              "%s: After fork in the child process, getpid() returns %d\n",
              __FUNCTION__,
              getpid());

      // Try out the pipe
      if (write(parent_read_end[WRITE_FD], "message from the child", strlen("message from the child")) <
          0) {
         fprintf(stderr, "Child couldn't write to pipe, %s\n", strerror(errno));
      }
      if (read(parent_write_end[READ_FD], buf, sizeof(buf)) < 0) {
         fprintf(stderr, "Child couldn't read from pipe, %s\n", strerror(errno));
      } else {
         fprintf(stdout, "Child %d read: <%s> from parent\n", getpid(), buf);
         fflush(stdout);
      }

      // Try out execve()
      execve(HELLO_WORLD_TEST, args, env);
      fprintf(stderr, "%s: execve() failed %s\n", __FUNCTION__, strerror(errno));
      exit(1);
   } else {
      printf("After fork in the parent, child pid %d\n", pid);
      assert(pid > 0);
      // Try out the pipe
      if (write(parent_write_end[WRITE_FD],
                "message from the parent",
                strlen("message from the parent")) < 0) {
         fprintf(stderr, "Parent couldn't write to pipe, %s\n", strerror(errno));
      }
      if (read(parent_read_end[READ_FD], buf, sizeof(buf)) < 0) {
         fprintf(stderr, "Parent couldn't read from pipe, %s\n", strerror(errno));
      } else {
         fprintf(stdout, "Parent %d read: <%s> from child\n", getpid(), buf);
         fflush(stdout);
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
         fprintf(stderr,
                 "%s: waitpid() returned wrong pid, expected %d, got %d\n",
                 __FUNCTION__,
                 pid,
                 siginfo.si_pid);
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
      exit(1);
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
         fprintf(stderr,
                 "%s: waitpid() returned wrong pid, expected %d, got %d\n",
                 __FUNCTION__,
                 pid,
                 siginfo.si_pid);
         return 1;
      }
   }
   return 0;
}

/*
 * fork down a few levels verifying that getppid() returns the value we think it should.
 * Returns:
 *   0 - parent pids are as expected
 *   != 0 - unexpected parent pid
 */
int getppid_fork_test_0(void)
{
   pid_t expected_ppid;
   int current_fork_depth = 0;
   int max_fork_depth = 5;   // arbitrarily chosen

   // Output delimiter
   fprintf(stderr, "\nTest: %s\n", __FUNCTION__);

fork_again:;
   expected_ppid = getpid();
   pid_t fpid = fork();
   if (fpid < 0) {
      exit(1);
   }
   if (fpid == 0) {   // child process, verify parent pid, try to fork again
      pid_t parentpid = getppid();
      if (parentpid != expected_ppid) {
         fprintf(stderr, "expected parent pid %d, getppid() returned %d\n", expected_ppid, parentpid);
         exit(1);   // parent pid is not as expected
      }
      if (++current_fork_depth <= max_fork_depth) {
         fprintf(stderr,
                 "current_fork_depth %d, max_fork_depth %d, expected_ppid %d, parentpid %d\n",
                 current_fork_depth,
                 max_fork_depth,
                 expected_ppid,
                 parentpid);
         goto fork_again;
      }
      exit(0);

   } else {   // the parent process, wait for child to finish
      siginfo_t siginfo;
      memset(&siginfo, 0, sizeof(siginfo));
      int rc = waitid(P_PID, fpid, &siginfo, WEXITED);
      fprintf(stderr,
              "waitid() returned %d, errno %d, siginfo.si_pid %d, si_status 0x%x\n",
              rc,
              errno,
              siginfo.si_pid,
              siginfo.si_status);
      if (rc < 0) {
         fprintf(stderr, "%s: waitpid() for pid %d failed, %s\n", __FUNCTION__, fpid, strerror(errno));
         if (getpid() == 1) {
            return 1;
         }
         exit(1);
      }
      if (siginfo.si_pid != fpid) {
         fprintf(stderr,
                 "%s: waitpid() returned wrong pid, expected %d, got %d\n",
                 __FUNCTION__,
                 fpid,
                 siginfo.si_pid);
         if (getpid() == 1) {
            return 1;
         }
         exit(1);
      }
      // check to see if the child thought parent pid was ok.
      int rv = (WIFEXITED(siginfo.si_status) && WEXITSTATUS(siginfo.si_status) == 0) ? 0 : 1;
      if (rv != 0) {
         fprintf(stderr, "%s: unexpected exit status 0x%x, pid %d\n", __FUNCTION__, siginfo.si_status, getpid());
      }
      if (getpid() == 1) {
         return rv;
      } else {
         exit(rv);
      }
   }
}

int fork_kill_test_0_got_signal = 0;
pid_t fork_kill_test_0_sender_pid;
void fork_kill_test_0_signal_handler(int signo, siginfo_t* siginfo, void* ucontext)
{
   fork_kill_test_0_sender_pid = siginfo->si_pid;
   fork_kill_test_0_got_signal = 1;
}

/*
 * Test xlate of the pid argument passed to the kill hypercall.
 * We fork a child then the child will wait for a signal.  The parent will send a signal to
 * the child.
 */
int fork_kill_test_0(void)
{
   // Output delimiter
   fprintf(stderr, "\nTest: %s\n", __FUNCTION__);

   // install signal handler, the child process will inherit.
   struct sigaction sa = {.sa_sigaction = fork_kill_test_0_signal_handler, .sa_flags = SA_SIGINFO};
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGUSR1, &sa, NULL) < 0) {
      fprintf(stderr, "%s: couldn't install signal handler, %s\n", __FUNCTION__, strerror(errno));
      exit(1);
   }

   pid_t child_pid = fork();
   if (child_pid < 0) {
      fprintf(stderr, "%s: Fork failed, errno %d\n", __FUNCTION__, errno);
      return 1;
   }
   int rv = 0;
   if (child_pid == 0) {   // child process
#if 0
      /*
       * payload pause() doesn't seem to work as it should.  So, for now we sleep to allow the
       * signal time to arrive.  This probably will give endless trouble in azure.  Bleh!
       */
      pause();             // wait for signal arrival.
#else
      struct timespec tv;
      tv.tv_sec = 1;
      tv.tv_nsec = 0;
      nanosleep(&tv, NULL);
#endif
      fprintf(stderr, "%s: fork_kill_test_0_got_signal %d, fork_kill_test_0_sender_pid %d, parent pid %d\n",
              __FUNCTION__,
              fork_kill_test_0_got_signal,
              fork_kill_test_0_sender_pid,
              getppid());
      if (fork_kill_test_0_got_signal != 0) {    // check for signal 
         exit(0);
      } else {
         fprintf(stderr, "%s: expected signal did not arrive, pid %d\n", __FUNCTION__, getpid());
         exit(1);
      }
   } else {                // parent process
#if 1
      /*
       * Pause before sending a signal to the child.  This gives the fork a chance to complete.
       * This should be handled by blocking signals during fork processing. This is a stopgap measure!
       */
      struct timespec tv;
      tv.tv_sec = 0;
      tv.tv_nsec = 50000000;
      nanosleep(&tv, NULL);
#endif
      fprintf(stderr, "%s: pid %d has forked child %d\n", __FUNCTION__, getpid(), child_pid);
      // send signal to the child
      if (kill(child_pid, SIGUSR1) != 0) {
         fprintf(stderr, "%s: kill to pid %d failed, %s\n", __FUNCTION__, child_pid, strerror(errno));
         return 1;
      }
      // Wait for child to terminate
      siginfo_t siginfo;
      memset(&siginfo, 0, sizeof(siginfo));
      int rc = waitid(P_PID, child_pid, &siginfo, WEXITED);
      fprintf(stderr, "waitid() returned %d, errno %d, siginfo.si_pid %d\n", rc, errno, siginfo.si_pid);
      if (rc < 0) {
         fprintf(stderr, "%s: waitpid() for pid %d failed, %s\n", __FUNCTION__, child_pid, strerror(errno));
         return 1;
      }
      if (siginfo.si_pid != child_pid) {
         fprintf(stderr,
                 "%s: waitpid() returned wrong pid, expected %d, got %d\n",
                 __FUNCTION__,
                 child_pid,
                 siginfo.si_pid);
         return 1;
      }
      // See if child received the signal
      if (WIFEXITED(siginfo.si_status) && WEXITSTATUS(siginfo.si_status) == 0) {
         rv = 0;
      } else {
         fprintf(stderr, "%s: unexpected exit status 0x%x, from child pid %d\n", __FUNCTION__, siginfo.si_status, child_pid);
         rv = 1;
      }
   }
   return rv;
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
   if (stat(HELLO_WORLD_TEST, &statb) != 0) {   // Can we find hello_world?
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

   rvtmp = fork_kill_test_0();
   if (rv == 0) {
      rv = rvtmp;
   }

   rvtmp = getppid_fork_test_0();
   if (rv == 0) {
      rv = rvtmp;
   }

   close(parent_write_end[0]);
   close(parent_write_end[1]);
   close(parent_read_end[0]);
   close(parent_read_end[1]);

   return rv;
}
