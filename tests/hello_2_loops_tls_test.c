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
#include <errno.h>
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

int fedora;   // set to 1 if argv[0] is *.fedora

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
   // TLS layout is different for glibc and musl
   int allowed = fedora ? 256 : roundup(sizeof(my_str) + sizeof(msg), 16);

   // knowing how things are placed, check the addresses
   delta = PTHREAD_SELF() - (long)MIN(my_str, &my_msg);
   if (delta <= 0 || delta > allowed) {
      return 2;
   }
   // now check for expected content
   sprintf(str + strlen(str), "%lx", PTHREAD_SELF());
   if (strcmp(str, my_str) != 0) {
      return 1;
   }
   return strcmp(my_msg, msg);
}

static const char* const brick_msg = "brick in the wall ";
static const char* const dust_msg = "one bites the dust";
static const long step = 1ul;

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
      int rc;

      if ((rc = pthread_create(&pt1, NULL, (void* (*)(void*))subrun, (void*)brick_msg)) != 0) {
         printf("pthread_create() %d, %s", rc, strerror(errno));
         return (void*)(long)rc;
      }
      if ((rc = pthread_create(&pt2, NULL, (void* (*)(void*))subrun, (void*)dust_msg)) != 0) {
         printf("pthread_create() %d, %s", rc, strerror(errno));
         return (void*)(long)rc;
      }

      pthread_join(pt2, &rc2);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined %p %ld\n", (void*)pt2, (long)rc2);
      }
      pthread_join(pt1, &rc1);
      if (greatest_get_verbosity() != 0) {
         printf(" ... joined %p %ld\n", (void*)pt1, (long)rc1);
      }
      if (rc1 != NULL || rc2 != NULL) {
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
   ASSERT_EQ_FMT(NULL, rc1, "%p");

   if (greatest_get_verbosity() != 0) {
      printf("joining %p ... \n", (void*)pt2);
   }
   ret = pthread_join(pt2, &rc2);
   ASSERT_EQ(ret, 0);
   if (greatest_get_verbosity() != 0) {
      printf("joined %p, %d\n", (void*)pt2, ret);
   }
   ASSERT_EQ_FMT(NULL, rc2, "%p");

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args
   // greatest_set_verbosity(1);

   if (strstr(argv[0], ".fedora") != NULL) {
      fedora = 1;
   }
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(nested_threads);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}