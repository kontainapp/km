#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <linux/futex.h>
#include <time.h>
#include <errno.h>

#define ITERATIONS (5 * 1000 * 1000)

void display_delta(char* tag, struct timespec* start, struct timespec* end)
{
   double startf = start->tv_sec + (start->tv_nsec / 1000000000.);
   double endf = end->tv_sec + (end->tv_nsec / 1000000000.);

   fprintf(stdout, "%s: %.9f seconds\n", tag, endf - startf);
}

int main(int argc, char* argv[])
{
   int i;
   struct timespec start;
   struct timespec now;

   // A system call with no args
   pid_t pid;
   pid_t oldpid = getpid();
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (i = 0; i < ITERATIONS; i++) {
      pid = getpid();
      if (pid != oldpid) {
         fprintf(stderr, "Unexpected pid\n");
         break;
      }
      oldpid = pid;
   }
   clock_gettime(CLOCK_MONOTONIC, &now);
   display_delta("getpid time:", &start, &now);

   // A system call with 1 argument
   int badfd = 999;
   close(badfd);
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (i = 0; i < ITERATIONS; i++) {
     int rc = close(badfd);
      if (rc != -1 || errno != EBADF) {
         fprintf(stderr, "close did not fail, rc %d, errno %d\n", rc, errno);
         __builtin_trap();
      }
   }
   clock_gettime(CLOCK_MONOTONIC, &now);
   display_delta("close bad fd time:", &start, &now);

   // A system call with 6 arguments
   int fut = 99;
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (i = 0; i < ITERATIONS; i++) {
      errno = 0;
      int rc = syscall(SYS_futex, &fut, FUTEX_WAIT, fut-1, NULL, NULL, 43);
      if (rc != -1 || errno != EAGAIN) {
         fprintf(stderr, "SYS_futex didn't fail as expected\n");
         break;
      }
   }
   clock_gettime(CLOCK_MONOTONIC, &now);
   display_delta("futex time:", &start, &now);

   return 0;
}
