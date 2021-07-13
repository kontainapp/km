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
 * This is a small program that runs 3 syscalls/hypercalls that hopefully do very little in
 * the kernel.  The intent is to measure in a squishy way how adding arguments to syscalls
 * increases the overhead.  The syscalls chosen have no arguments, 1 argument, and 6 arguments.
 * Each syscall is run 5 million times and the length of time is measured and printed.
 * In addition 1 or more threads can be doing these syscall runs concurrently to see how
 * syscall argument passing scales.  You might think scaling effects should be minimal and
 * they are for the current km scheme of allocating km_hc_arg_t's.  We were considering having
 * a pool of km_hc_arg_t's and allocating one for each syscall and then freeing it when the
 * syscall returns.  This method would have affects on scaling.  This was being considered for
 * running go programs under km.
 *
 * Running this program by itself just gives you a set of data points for a particular way
 * of passing hypercall arguments to km.  This program should be run for each hypercall arg
 * passing method to get timing data for each and then comparisons can be made to decide
 * which method is better.
 *
 * One more thing to consider when trying to measure syscall overhead is you need to actually
 * disassemble the syscall c library function to see what is really going on.  Some of the
 * c library functions that invoke a syscall actually do more than you would think.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <linux/futex.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>

#define ITERATIONS (5 * 1000 * 1000)

pid_t gettid(void)
{
   return syscall(SYS_gettid);
}

void display_delta(char* tag, struct timespec* start, struct timespec* end)
{
   double startf = start->tv_sec + (start->tv_nsec / 1000000000.);
   double endf = end->tv_sec + (end->tv_nsec / 1000000000.);

   fprintf(stdout, "tid %3d, %s: %.9f seconds\n", gettid(), tag, endf - startf);
}

void* dosyscalls(void* arg)
{
   int i;
   struct timespec start;
   struct timespec now;

   // A system call with no args
   pid_t tid;
   pid_t oldtid = gettid();
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (i = 0; i < ITERATIONS; i++) {
      tid = gettid();
      if (tid != oldtid) {
         fprintf(stderr, "Unexpected tid\n");
         __builtin_trap();
      }
      oldtid = tid;
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
         __builtin_trap();
      }
   }
   clock_gettime(CLOCK_MONOTONIC, &now);
   display_delta("futex time:", &start, &now);

   return (void*)9999;
}

#define NTHREADS 280
int main(int argc, char* argv[])
{
   int i;
   int rc;
   int nthreads;
   pthread_t threadids[NTHREADS];
   struct timespec start;
   struct timespec now;

   if (argc == 2) {
      nthreads = atoi(argv[1]);
   } else {
      nthreads = NTHREADS;
   }
   if (nthreads > NTHREADS) {
      fprintf(stderr, "Maximum number of threads is %d, if you need more change this programs\n", NTHREADS);
      return 1;
   }
   printf("Running tests with %d threads, %d iterations\n", nthreads, ITERATIONS);

   clock_gettime(CLOCK_MONOTONIC, &start);

   // Start a gaggle of threads
   for (i = 0; i < nthreads; i++) {
      rc = pthread_create(&threadids[i], NULL, dosyscalls, NULL);
      if (rc != 0) {
         fprintf(stderr, "Couldn't create thread %d, %s\n", i, strerror(errno));
         threadids[i] = 0;
      }
   }

   // Wait for the threads to finish
   for (i = 0; i < nthreads; i++) {
      void* retval;
      rc = pthread_join(threadids[i], &retval);
      if (rc != 0) {
         fprintf(stderr, "Couldn't join thread %d, %s\n", i, strerror(errno));
      }
   }

   clock_gettime(CLOCK_MONOTONIC, &now);
   display_delta("total time:", &start, &now);

   return 0;
}
