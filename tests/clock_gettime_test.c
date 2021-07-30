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

#define _GNU_SOURCE
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "greatest/greatest.h"
#include "syscall.h"

// Do clock_gettime() through VDSO and then through system call. Make sure subsequent calls are monotonic

static const int count = 100;

static inline u_int64_t nsecs(struct timespec ts)
{
   return ts.tv_sec * 1000000000l + ts.tv_nsec;
}

static inline void print_ts(char* msg, struct timespec ts1, struct timespec ts2)
{
   printf("%s: %ld.%9ld\t%ld.%9ld\n", msg, ts1.tv_sec, ts1.tv_nsec, ts2.tv_sec, ts2.tv_nsec);
}

TEST test(void)
{
   struct timespec ts1, ts2;

   for (int i = 0; i < count; i++) {
      clock_gettime(CLOCK_MONOTONIC, &ts1);
      syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts2);
      if (greatest_get_verbosity() != 0 && !(nsecs(ts1) <= nsecs(ts2))) {
         print_ts("vdso > syscall", ts1, ts2);
      }
      ASSERT(nsecs(ts1) <= nsecs(ts2));
   }
   for (int i = 0; i < count; i++) {
      syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts1);
      clock_gettime(CLOCK_MONOTONIC, &ts2);
      if (greatest_get_verbosity() != 0 && !(nsecs(ts1) <= nsecs(ts2))) {
         print_ts("syscall > vdso", ts1, ts2);
      }
      ASSERT(nsecs(ts1) <= nsecs(ts2));
   }
   // tighter loop to make sure clock_gettime VDSO is monotonic
   for (int i = 0; i < count; i++) {
      clock_gettime(CLOCK_MONOTONIC, &ts1);
      clock_gettime(CLOCK_MONOTONIC, &ts2);
      if (greatest_get_verbosity() != 0 && !(nsecs(ts1) <= nsecs(ts2))) {
         print_ts("vdso > vdso", ts1, ts2);
      }
      ASSERT(nsecs(ts1) <= nsecs(ts2));
   }
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(test);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
