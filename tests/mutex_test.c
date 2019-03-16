/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <err.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "greatest/greatest.h"
#include "syscall.h"

volatile int var;
pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
const long step = 1l << 23;
const long run_count = 1l << 24;

// actual testing. return type is TEST since ASSERT_* return this
TEST run(void* arg)
{
   int x, y;
   struct timespec tp, tp0;
   char* msg = arg == NULL ? " child " : "parent ";

   clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp0);

   for (long i = 0; i < run_count; i++) {
      pthread_mutex_lock(&mt);
      if (arg == NULL) {
         x = ++var;
      } else {
         x = --var;
      }
      y = var;
      pthread_mutex_unlock(&mt);
      ASSERT_EQ_FMTm(msg, x, y, "%d");
      if ((i & (step - 1)) == 0) {
         clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
         printf("%s: %ld, %ld ms\n",
                msg,
                i / step,
                tp.tv_nsec > tp0.tv_nsec
                    ? (tp.tv_sec - tp0.tv_sec) * 1000 + (tp.tv_nsec - tp0.tv_nsec) / 1000000
                    : (tp.tv_sec - tp0.tv_sec) * 1000 +
                          (1000000000 - tp0.tv_nsec + tp.tv_nsec) / 1000000);
         tp0 = tp;
      }
   }
   PASS();
}

// thread entry. It calls actual test with typecast, to make compiler happy
void* run_thr(void* arg)
{
   return (void*)run(arg);
}

// let's use a couple of TESTS, just to have more that 1 test in the file
static pthread_t pt1, pt2;

TEST basic_create(void)
{
   ASSERT_EQm("Create 1 ok", pthread_create(&pt1, NULL, run_thr, NULL), 0);
   ASSERT_EQm("Create 2 ok", pthread_create(&pt2, NULL, run_thr, (void*)1), 0);
   PASSm("threads started\n");
}

TEST run_and_check(void)
{
   void* retval;
   ASSERT_EQm("Joined 2 ok ", pthread_join(pt2, &retval), 0);
   ASSERT_EQm("Thread 2 test passed", retval, 0);
   ASSERT_EQm("Joined 1 ok", pthread_join(pt1, &retval), 0);
   ASSERT_EQm("Thread 1 test passed", retval, 0);
   PASSm("joined and checked\n");
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();       // init & parse command-line args
   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(basic_create);
   RUN_TEST(run_and_check);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}