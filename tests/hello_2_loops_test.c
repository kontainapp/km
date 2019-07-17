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
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "greatest/greatest.h"
#include "syscall.h"

pthread_key_t mystr_key, mystr_key_2;

void free_key(void* str)
{
   free((char*)str);
}

void mykeycreate(void)
{
   pthread_key_create(&mystr_key, free_key);
}

void make_mystr(char* msg)
{
   char* str;
   static pthread_once_t mykeycreated = PTHREAD_ONCE_INIT;

   pthread_once(&mykeycreated, mykeycreate);

   str = malloc(128);
   sprintf(str, "I'm 0x%lx", pthread_self());

   pthread_setspecific(mystr_key, str);
   pthread_setspecific(mystr_key_2, strdup(msg));
}

static const char* brick_msg = "brick in the wall ";
static const char* dust_msg = "one bites the dust";
const long step = 1ul;

void subrun(char* msg)
{
   make_mystr(msg);
   for (long run_count = 0; run_count < step; run_count++) {
      if (run_count % step == 0) {
         printf("Another %s # %ld of %ld, %s\n",
                (char*)pthread_getspecific(mystr_key_2),
                run_count / step,
                run_count,
                (char*)pthread_getspecific(mystr_key));
      }
   }
   pthread_exit(0);
}

void* run(void* msg)
{
   for (long run_count = 0; run_count < 1024; run_count++) {
      pthread_t pt1, pt2;
      pthread_attr_t attr;

      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 365 * 1024);
      pthread_create(&pt1, &attr, (void* (*)(void*))subrun, (void*)brick_msg);
      pthread_create(&pt2, &attr, (void* (*)(void*))subrun, (void*)dust_msg);
      pthread_attr_destroy(&attr);
      pthread_join(pt2, NULL);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined 0x%lx\n", pt2);
      }
      pthread_join(pt1, NULL);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined 0x%lx\n", pt1);
      }
   }
   return NULL;
}

TEST nested_threads(void)
{
   pthread_t pt1, pt2;
   int ret;

   pthread_key_create(&mystr_key_2, free_key);
   pthread_setspecific(mystr_key_2, (void*)0x17);

   ret = pthread_create(&pt1, NULL, run, NULL);
   // assert macro can use params more than once, so using separate <ret>
   ASSERT_EQ(0, ret);
   if (greatest_get_verbosity() != 0) {
      printf("started 0x%lx\n", pt1);
   }

   ret = pthread_create(&pt2, NULL, run, NULL);
   ASSERT_EQ(0, ret);
   if (greatest_get_verbosity() != 0) {
      printf("started 0x%lx\n", pt2);
   }

   if (greatest_get_verbosity() != 0) {
      printf("joining 0x%lx ... \n", pt1);
   }
   ret = pthread_join(pt1, NULL);
   ASSERT_EQ(0, ret);
   if (greatest_get_verbosity() != 0) {
      printf("joined 0x%lx, %d\n", pt1, ret);
   }

   if (greatest_get_verbosity() != 0) {
      printf("joining 0x%lx ... \n", pt2);
   }
   ret = pthread_join(pt2, NULL);
   ASSERT_EQ(0, ret);

   ASSERT_EQ((void*)0x17, pthread_getspecific(mystr_key_2));
   ASSERT_EQ(pthread_key_delete(mystr_key_2), 0);
   ASSERT_EQ(0, pthread_getspecific(mystr_key_2));

   if (greatest_get_verbosity() != 0) {
      printf("joined 0x%lx, %d\n", pt2, ret);
   }

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