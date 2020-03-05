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
   printf("Hello from clone\n");
   return 0; /* Child terminates now */
}

#define STACK_SIZE (1024 * 1024) /* Stack size for cloned child */

int main(int argc, char* argv[])
{
   unsigned flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
                    CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_DETACHED;
   char* stack;    /* Start of stack buffer */
   char* stackTop; /* End of stack buffer */
   pid_t pid, tid;

   stack = malloc(STACK_SIZE);
   if (stack == NULL)
      errExit("malloc");
   stackTop = stack + STACK_SIZE - 16; /* Assume stack grows downward */

   printf("clone()\n");

   pid = clone(childFunc, stackTop, flags, NULL, &pid, NULL, &tid);
   if (pid == -1)
      errExit("clone");

   printf("clone() returned %ld\n", (long)pid);

   usleep(1000);
   exit(EXIT_SUCCESS);
}
