/*
 * Copyright 2021 Kontain Inc.
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

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#define errExit(msg)                                                                               \
   do {                                                                                            \
      perror(msg);                                                                                 \
      exit(EXIT_FAILURE);                                                                          \
   } while (0)

static int childFunc(void* arg)
{
   fprintf(stderr, "Hello from clone\n");
   return 0; /* Child terminates now */
}

#define STACK_SIZE (1024 * 1024) /* Stack size for cloned child */

int main(int argc, char* argv[])
{
   unsigned flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
                    CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_DETACHED;
   char* stack; /* Start of stack buffer */
   pid_t pid, tid;

   stack = malloc(STACK_SIZE);
   if (stack == NULL) {
      errExit("malloc");
   }

   printf("clone()\n");
   pid = clone(childFunc, stack + STACK_SIZE, flags, NULL, &pid, NULL, &tid);
   if (pid == -1) {
      errExit("clone");
   }
   printf("clone() returned %ld\n", (long)pid);

   usleep(100000);
   exit(EXIT_SUCCESS);
}
