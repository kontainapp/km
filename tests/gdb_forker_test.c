/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * A test program that forks and then th child execs to a different program.
 * Used to test km's gdbstub use of the KM_GDB_CHILD_FORK_WAIT environment variable
 * to control where km pauses to wait for gdb client to attach
 * to a gdb stub instance.  And gdbstub's use of the "exec" stop reply which allows
 * gdbstub to tell the gdb client that an execve() system call has finished and a
 * new program is running.
 */

int main(int argc, char* argv[])
{
//   char payload[] = "tests/hello_test";
   char payload[] = "hello_test";
   pid_t pid;

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "fork() in %s failed, %s\n", argv[0], strerror(errno));
      return 1;
   } else if (pid == 0) {
      char* new_argv[2];
      new_argv[0] = payload;
      new_argv[1] = NULL;
      char* new_envp[1];
      new_envp[0] = NULL;
      fprintf(stderr, "Child pid %d exec()'ing to %s\n", getpid(), payload);
      execve(payload, new_argv, new_envp);
      fprintf(stderr, "execve() to %s, pid %d, failed %s\n", payload, getpid(), strerror(errno));
      return 1;
   } else { // parent
      fprintf(stderr, "Waiting for child pid %d to terminate\n", pid);
      pid_t waited_pid;
      int status;
      waited_pid = waitpid(pid, &status, 0);
      assert(waited_pid == pid);
      fprintf(stdout, "Child pid %d terminated with status %d (0x%x)\n", pid, status, status);
   }
   return 0;
}
