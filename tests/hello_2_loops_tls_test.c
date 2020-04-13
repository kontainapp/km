/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include "greatest/greatest.h"
#include "syscall.h"

__thread char my_str[128] = "I'm 0x";
__thread char* my_msg;

#define PTHREAD_SELF() ((uint64_t)pthread_self())

void make_mystr(char* msg)
{
   sprintf(my_str + strlen(my_str), "%lx", PTHREAD_SELF());
   my_msg = msg;
}

int check_mystr(char* msg)
{
   char str[128] = "I'm 0x";
   long delta;

   //  knowing how things are placed, check the addresses
   delta = PTHREAD_SELF() - (long)MIN(my_str, &my_msg);
   if (delta <= 0 || delta > roundup(sizeof(my_str) + sizeof(msg), 16)) {
      return 2;
   }
   // now check for expected content
   sprintf(str + strlen(str), "%lx", PTHREAD_SELF());
   if (strcmp(str, my_str) != 0) {
      return 1;
   }
   return strcmp(my_msg, msg);
}

static const char* brick_msg = "brick in the wall ";
static const char* dust_msg = "one bites the dust";
const long step = 1ul;

void* subrun(char* msg)
{
   make_mystr(msg);
   for (long run_count = 0; run_count < step; run_count++) {
      if (run_count % step == 0) {
         printf("Another %s # %ld of %ld, %s\n", my_msg, run_count / step, run_count, my_str);
         if (check_mystr(msg) != 0) {
            pthread_exit((void*)1);
         }
      }
   }
   return 0;
}

void* run(void* msg)
{
   for (long run_count = 0; run_count < 1024; run_count++) {
      pthread_t pt1, pt2;
      void* rc1;
      void* rc2;

      pthread_create(&pt1, NULL, (void* (*)(void*))subrun, (void*)brick_msg);
      pthread_create(&pt2, NULL, (void* (*)(void*))subrun, (void*)dust_msg);
      pthread_join(pt2, &rc2);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined %p %ld\n", (void*)pt2, (long)rc2);
      }
      pthread_join(pt1, &rc1);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined %p %ld\n", (void*)pt1, (long)rc1);
      }
      if (rc1 != 0 || rc2 != 0) {
         pthread_exit((void*)1);
      }
   }
   return NULL;
}

TEST nested_threads(void)
{
   pthread_t pt1, pt2;
   int ret;
   void* rc1;
   void* rc2;

   ret = pthread_create(&pt1, NULL, run, NULL);
   // assert macro can use params more than once, so using separate <ret>
   ASSERT_EQ(0, ret);
   if (greatest_get_verbosity() != 0) {
      printf("started %p\n", (void*)pt1);
   }

   ret = pthread_create(&pt2, NULL, run, NULL);
   ASSERT_EQ(0, ret);
   if (greatest_get_verbosity() != 0) {
      printf("started %p\n", (void*)pt2);
   }

   if (greatest_get_verbosity() != 0) {
      printf("joining %p ... \n", (void*)pt1);
   }
   ret = pthread_join(pt1, &rc1);
   ASSERT_EQ(ret, 0);
   if (greatest_get_verbosity() != 0) {
      printf("joined %p, %d\n", (void*)pt1, ret);
   }
   ASSERT_EQ(rc1, 0);

   if (greatest_get_verbosity() != 0) {
      printf("joining %p ... \n", (void*)pt2);
   }
   ret = pthread_join(pt2, &rc2);
   ASSERT_EQ(ret, 0);
   if (greatest_get_verbosity() != 0) {
      printf("joined %p, %d\n", (void*)pt2, ret);
   }
   ASSERT_EQ(rc2, 0);

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args
   // greatest_set_verbosity(1);

   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(nested_threads);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}