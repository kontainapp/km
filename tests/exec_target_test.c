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
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/epoll.h>

/*
 * This program tests that SIGCHLD is not accidently blocked in the child process
 * when the child process is forked.
 * To run:
 *   exec_target parent_of_waitforchild
 *
 * It will fork and exec a copy itself with the arg "waitforchild" and that process
 * will fork and exec a copy of itself with the arg "child"
 *
 * The middle child process is the one that we want to verify can receive SIGCHLD
 * signals.
 *
 * Exit value: 0 = success, non-zero = failure
 */

int handle_sigchld_entered = 0;
void handle_sigchld(int sig, siginfo_t *info, void *ucontext)
{
   char message[] = "handle_sigchld entered\n";
   // Just use write, fprintf() uses a mutex.
   write(2, message, strlen(message));
   handle_sigchld_entered = 1;
}

void usage(void)
{
   fprintf(stderr, "Usage: exec_target [parent_of_waitforchild | waitforchild | child]\n");
}

int main(int argc, char* argv[])
{
   pid_t pid;
   pid_t rv;
   int status;
   char thisprogram[PATH_MAX];
   int rc;

   if (argc < 2) {
      usage();
      return 1;
   }

   // Get the path to this program
   rc = readlink("/proc/self/exe", thisprogram, sizeof(thisprogram));
   if (rc < 0) {
      fprintf(stderr, "Couldn't readlink /proc/self/exe, %s\n", strerror(errno));
      return 1;
   }
   thisprogram[rc] = 0;

   fprintf(stderr, "pid %d, %s is running\n", getpid(), argv[1]);

   if (strcmp(argv[1], "parent_of_waitforchild") == 0) {
      // fork()
      // parent: wait for the child to finish
      // child: exec("exec_target", "waitforchild")
      pid = fork();
      if (pid < 0) {
         fprintf(stderr, "%s:%d: %s: fork failed, %s\n", __FUNCTION__, __LINE__, argv[1], strerror(errno));
         return 1;
      }
      if (pid != 0) {
         // we are the parent
         rv = waitpid(pid, &status, 0);
         if (rv == pid) {
            fprintf(stderr, "test complete, status 0x%x\n", status);
            return 0;
         }
         // waitpid failed
         fprintf(stderr, "%s:%d: %s: waitpid unexpected return value, rv %d, errno %d, status 0x%x\n", __FUNCTION__, __LINE__, argv[1], rv, errno, status);
         return 1;
      }
      // We are the child
      char* argv[] = { "exec_target", "waitforchild", NULL };
      char* envv[] = { NULL };
      rc = execve(thisprogram, argv, envv);
      fprintf(stderr, "execve() to %s failed, %s\n", thisprogram, strerror(errno));
      return 1;
   }


   if (strcmp(argv[1], "waitforchild") == 0) {
      // fork()
      // parent: setup SIGCHLD signal handler, block in epoll_pwait(), SIGCHLD interrupts epoll_pwait(), exit
      // child: fork() then exec("exec_target", "child")
      pid = fork();
      if (pid < 0) {
         fprintf(stderr, "%s:%d: %s: fork failed, %s\n", __FUNCTION__, __LINE__, argv[1], strerror(errno));
         return 1;
      }
      if (pid != 0) {
         // we are the parent
         struct sigaction act = { .sa_sigaction = handle_sigchld, .sa_flags = SA_SIGINFO };
         rc = sigaction(SIGCHLD, &act, NULL);
         if (rc < 0) {
            fprintf(stderr, "sigaction failed, %s\n", strerror(errno));
            return 1;
         }
         int epoll_thing;
         epoll_thing = epoll_create(1);
         if (epoll_thing < 0) {
            fprintf(stderr, "epoll_create failed, %s\n", strerror(errno));
            return 1;
         }
         int epoll_target_fd[2];
         rc = pipe(epoll_target_fd);
         struct epoll_event epoll_event = { EPOLLIN };
         epoll_event.data.fd = epoll_target_fd[0];
         rc = epoll_ctl(epoll_thing, EPOLL_CTL_ADD, epoll_target_fd[0], &epoll_event);
         if (rc < 0) {
            fprintf(stderr, "epoll_ctl failed, %s\n", strerror(errno));
            return 1;
         }
         struct epoll_event got_this;
         rc = epoll_pwait(epoll_thing, &got_this, 1, -1, NULL);
         if (rc < 0 && errno == EINTR) {
            fprintf(stderr, "epoll_wait() interrupted\n");
            if (handle_sigchld_entered == 0) {
               fprintf(stderr, "handle_sigchld_entered is not expected to be 0, but is\n");
               return 1;
            }
            fprintf(stderr, "handle_sigchld_entered is non-zero as expected\n");
            return 0;
         }
         fprintf(stderr, "epoll_wait() returned %d, not expected!\n", rc);
         return 1;
      }
      // We are the child
      char* argv[] = { "exec_target", "child", NULL };
      char* envv[] = { NULL };
      rc = execve(thisprogram, argv, envv);
      fprintf(stderr, "execve() to %s failed, %s\n", thisprogram, strerror(errno));
      return 1;
   }


   if (strcmp(argv[1], "child") == 0) {
      // do nothing for a little while
      // exit
      sleep(1);
      return 0;
   }

   // Some argument we don't understand
   usage();

   return 1;
}
