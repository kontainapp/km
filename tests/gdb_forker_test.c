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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

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
   extern char** environ;
   //   char payload[] = "tests/hello_test";
   char payload[] = "hello_test.km";
   pid_t pid;

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "fork() in %s failed, %s\n", argv[0], strerror(errno));
      return 1;
   } else if (pid == 0) {
      char* new_argv[2];
      new_argv[0] = payload;
      new_argv[1] = NULL;
      fprintf(stderr, "Child pid %d exec()'ing to %s\n", getpid(), payload);
      execve(payload, new_argv, environ);
      fprintf(stderr, "execve() to %s, pid %d, failed %s\n", payload, getpid(), strerror(errno));
      return 1;
   } else {   // parent
      fprintf(stderr, "Waiting for child pid %d to terminate\n", pid);
      pid_t waited_pid;
      int status;
      waited_pid = waitpid(pid, &status, 0);
      assert(waited_pid == pid);
      fprintf(stdout, "Child pid %d terminated with status %d (0x%x)\n", pid, status, status);
   }
   return 0;
}
