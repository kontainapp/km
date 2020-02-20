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
#include <sys/auxv.h>
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

// 1 million iterations times out on azure
#define NTIMES	10000

int main(int argc, char* argv[])
{
   int i;
   int r;
   struct timespec ts;
   unsigned cpu;
   struct timespec start, end;
   struct timespec syscall_duration;
   struct timespec funccall_duration;
   struct timespec sleep_duration;
   struct timespec sleep_this_long = { 1, 0 };
   double sysdur_float;
   double funcdur_float;
   double sleepdur_float;
   uint64_t vdso_base;

   vdso_base = getauxval(AT_SYSINFO_EHDR);
   if (vdso_base == 0) {
      printf("auxv[AT_SYSINFO_EHDR] not available?\n");
      return 0;
   }
   printf("auxv[AT_SYSINFO_EHDR] = 0x%lx\n", vdso_base);

   // Verify that time advances using clock_gettime() from the vdso
   r = clock_gettime(CLOCK_REALTIME, &start);
   assert(r == 0);
   r = nanosleep(&sleep_this_long, NULL);
   assert(r == 0);
   r = clock_gettime(CLOCK_REALTIME, &end);
   assert(r == 0);
   timespec_sub(&sleep_duration, &end, &start);
   sleepdur_float = (double)sleep_duration.tv_sec + ((double)sleep_duration.tv_nsec / 1000000000.);
#define ALLOWED_SLEEP_DEVIATION 0.10
   if (sleepdur_float < ((1.0 - ALLOWED_SLEEP_DEVIATION) * sleep_this_long.tv_sec) ||
       sleepdur_float > ((1.0 + ALLOWED_SLEEP_DEVIATION) * sleep_this_long.tv_sec)) {
      printf("expected sleep duration between %f and %f seconds, actual %f\n",
             ((1.0 - ALLOWED_SLEEP_DEVIATION) * sleep_this_long.tv_sec),
             ((1.0 + ALLOWED_SLEEP_DEVIATION) * sleep_this_long.tv_sec),
             sleepdur_float);
   }

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
   printf("SYS_clock_gettime syscall duration %ld.%09ld seconds for %d iterations\n",
          syscall_duration.tv_sec, syscall_duration.tv_nsec,
          NTIMES);
   printf("clock_gettime() func call duration %ld.%09ld seconds for %d iterations\n",
          funccall_duration.tv_sec,
          funccall_duration.tv_nsec, NTIMES);
#define VDSO_CG_FACTOR 10.0
   if (funcdur_float * VDSO_CG_FACTOR > sysdur_float) {
      printf("clock_gettime() %f exceeds 1/%f the time of SYS_clock_gettime %f\n", funcdur_float, VDSO_CG_FACTOR, sysdur_float);
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
   printf("SYS_getcpu syscall duration %ld.%09ld seconds for %d iterations\n",
          syscall_duration.tv_sec,
          syscall_duration.tv_nsec, NTIMES);
   printf("sched_getcpu() func call duration %ld.%09ld seconds for %d iterations\n",
          funccall_duration.tv_sec,
          funccall_duration.tv_nsec, NTIMES);
#define VDSO_SGC_FACTOR 9.0
   if (funcdur_float * VDSO_SGC_FACTOR > sysdur_float) {
      printf("sched_getcpu() %f exceeds 1/%f the time of SYS_getcpu %f\n", funcdur_float, VDSO_SGC_FACTOR, sysdur_float);
   }

   return 0;
}
