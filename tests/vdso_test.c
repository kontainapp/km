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
 *
 */

/*
 * This test exercises system calls that may have been made into function
 * calls that reside in the vdso memory segment mapped into the payload
 * address space
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include "syscall.h"

void timespec_sub(struct timespec *result, struct timespec *subfrom, struct timespec *subme)
{
  if (subme->tv_nsec > subfrom->tv_nsec) {
     result->tv_nsec = (subfrom->tv_nsec + 1000000000ul) - subme->tv_nsec;
     result->tv_sec = subfrom->tv_sec - subme->tv_sec - 1;
  } else {
     result->tv_nsec = subfrom->tv_nsec - subme->tv_nsec;
     result->tv_sec = subfrom->tv_sec - subme->tv_sec;
  }
}

#define NTIMES	1000000

int main(int argc, char* argv[])
{
   int i;
   int r;
   struct timespec ts;
   unsigned cpu;
   struct timespec start, end;
   struct timespec syscall_duration;
   struct timespec funccall_duration;
   double sysdur_float;
   double funcdur_float;

   // Run syscall(SYS_clock_gettime,...) a few times
   r = clock_gettime(CLOCK_REALTIME, &start);
   assert(r == 0);
   for (i = 0; i < NTIMES; i++) {
      long ts32[2];
      r = syscall(SYS_clock_gettime, CLOCK_REALTIME, ts32);
      assert(r == 0);
      ts.tv_sec = ts32[0];
      ts.tv_nsec = ts32[1];
   }
   r = clock_gettime(CLOCK_REALTIME, &end);
   assert(r == 0);
   timespec_sub(&syscall_duration, &end, &start);
   sysdur_float = (double)syscall_duration.tv_sec + ((double)syscall_duration.tv_nsec / 1000000000.);

   // Run clock_gettime(...) a few times
   r = clock_gettime(CLOCK_REALTIME, &start);
   assert(r == 0);
   for (i = 0; i < NTIMES; i++) {
      r = clock_gettime(CLOCK_REALTIME, &ts);
      assert(r == 0);
   }
   r = clock_gettime(CLOCK_REALTIME, &end);
   assert(r == 0);
   timespec_sub(&funccall_duration, &end, &start);
   funcdur_float = (double)funccall_duration.tv_sec + ((double)funccall_duration.tv_nsec / 1000000000.);

   // Compare the duration of the runs to see if clock_gettime() is faster
   printf("\n");
   printf("SYS_clock_gettime syscall duration %ld.%09ld seconds for %d iterations\n", syscall_duration.tv_sec, syscall_duration.tv_nsec, NTIMES);
   printf("clock_gettime() func call duration %ld.%09ld seconds for %d iterations\n", funccall_duration.tv_sec, funccall_duration.tv_nsec, NTIMES);
   if (funcdur_float > sysdur_float / 80.) {
      printf("clock_gettime() %f exceeds 1/80 the time of SYS_clock_gettime %f\n", funcdur_float, sysdur_float);
   }


   // Run sched_getcpu() a few times.
   r = clock_gettime(CLOCK_REALTIME, &start);
   assert(r == 0);
   for (i = 0; i < NTIMES; i++) {
      cpu = sched_getcpu();
   }
   r = clock_gettime(CLOCK_REALTIME, &end);
   assert(r == 0);
   timespec_sub(&funccall_duration, &end, &start);
   funcdur_float = (double)funccall_duration.tv_sec + ((double)funccall_duration.tv_nsec / 1000000000.);

   // Run syscall(SYS_getcpu, .. ) a few times
   r = clock_gettime(CLOCK_REALTIME, &start);
   assert(r == 0);
   for (i = 0; i < NTIMES; i++) {
      r = syscall(SYS_getcpu, &cpu, 0, 0);
      assert(r == 0);
   }
   r = clock_gettime(CLOCK_REALTIME, &end);
   assert(r == 0);
   timespec_sub(&syscall_duration, &end, &start);
   sysdur_float = (double)syscall_duration.tv_sec + ((double)syscall_duration.tv_nsec / 1000000000.);

   printf("\n");
   printf("SYS_getcpu syscall duration %ld.%09ld seconds for %d iterations\n", syscall_duration.tv_sec, syscall_duration.tv_nsec, NTIMES);
   printf("sched_getcpu() func call duration %ld.%09ld seconds for %d iterations\n", funccall_duration.tv_sec, funccall_duration.tv_nsec, NTIMES);
   if (funcdur_float > sysdur_float / 60) {
      printf("sched_getcpu() %f exceeds 1/60 the time of SYS_getcpu %f\n", funcdur_float, sysdur_float);
   }

   return 0;
}
