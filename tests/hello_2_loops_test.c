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
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "greatest/greatest.h"
#include "syscall.h"

static const char* brick_msg = "brick in the wall ";
static const char* dust_msg = "one bites the dust";
const long step = 2ul;

void subrun(char* msg)
{
   for (long run_count = 0; run_count < step; run_count++) {
      if (run_count % step == 0) {
         printf("Another %s # %ld (%ld)\n", msg, run_count / step, run_count);
      }
   }
   pthread_exit(0);
}

void* run(void* msg)
{
   for (long run_count = 0; run_count < 1024; run_count++) {
      pthread_t pt1, pt2;

      pthread_create(&pt1, NULL, (void* (*)(void*))subrun, (void*)brick_msg);
      pthread_create(&pt2, NULL, (void* (*)(void*))subrun, (void*)dust_msg);
      printf(" ... joined 0x%lx, %d\n", pt2, pthread_join(pt2, NULL));
      printf(" ... joined 0x%lx, %d\n", pt1, pthread_join(pt1, NULL));
   }
   return NULL;
}

TEST nested_threads(void)
{
   pthread_t pt1, pt2;
   int ret;

   ret = pthread_create(&pt1, NULL, run, NULL);
   // assert macro can use params more than once, so using separate <ret>
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt1);
   ret = pthread_create(&pt2, NULL, run, NULL);
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt2);

   printf("joining 0x%lx ... \n", pt1);
   ASSERT_EQ(ret, 0);
   printf("joined 0x%lx, %d\n", pt1, ret = pthread_join(pt1, NULL));

   printf("joining 0x%lx ... \n", pt2);
   printf("joined 0x%lx, %d\n", pt2, ret = pthread_join(pt2, NULL));
   ASSERT_EQ(ret, 0);

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();       // init & parse command-line args
   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(nested_threads);

   GREATEST_MAIN_END();           // display results
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}