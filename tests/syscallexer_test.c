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
   printf("Running tests with %d threads\n", nthreads);

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
