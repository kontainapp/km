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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "greatest/greatest.h"
#include "syscall.h"

int detached = 0;
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
   sprintf(str, "I'm %p", (void*)pthread_self());

   pthread_setspecific(mystr_key, str);
   pthread_setspecific(mystr_key_2, strdup(msg));
}

static const char* const brick_msg = "brick in the wall ";
static const char* const dust_msg = "one bites the dust";
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
   /*
    * For joinable threads we first join and then fire a new one, so total number of threads is
    * limited to 7, 1024 is just number if iterations. But with detached there is no way to know
    * thread is complete and it is possible *all* of the started will be running in parallel, so we
    * limit it to 128 here not to run out of VCPUs.
    */
   for (long run_count = 0; run_count < (detached == 0 ? 1024 : 128); run_count++) {
      pthread_t pt1, pt2;
      pthread_attr_t attr;

      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 365 * 1024);
      if (detached == 1) {
         pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      }
      int rc;

      if ((rc = pthread_create(&pt1, &attr, (void* (*)(void*))subrun, (void*)brick_msg)) != 0) {
         printf("pthread_create() %d, %s", rc, strerror(errno));
         return (void*)(long)rc;
      }
      if ((rc = pthread_create(&pt2, &attr, (void* (*)(void*))subrun, (void*)dust_msg)) != 0) {
         printf("pthread_create() %d, %s", rc, strerror(errno));
         return (void*)(long)rc;
      }
      pthread_attr_destroy(&attr);
      if (detached == 0) {
         pthread_join(pt2, NULL);
         if (greatest_get_verbosity() != 0) {
            printf(" ... joined %p\n", (void*)pt2);
         }
         pthread_join(pt1, NULL);
         if (greatest_get_verbosity() != 0) {
            printf(" ... joined %p\n", (void*)pt1);
         }
      }
   }
   return NULL;
}

TEST nested_threads(void)
{
   pthread_t pt1, pt2;
   int ret;
   void* status;
   static const void* const ptr = (void*)0x17;

   pthread_key_create(&mystr_key_2, free_key);
   pthread_setspecific(mystr_key_2, ptr);

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
   ret = pthread_join(pt1, &status);
   ASSERT_EQ(0, ret);
   ASSERT_EQ_FMT(NULL, status, "%p");
   if (greatest_get_verbosity() != 0) {
      printf("joined %p, %d\n", (void*)pt1, ret);
   }

   if (greatest_get_verbosity() != 0) {
      printf("joining %p ... \n", (void*)pt2);
   }
   ret = pthread_join(pt2, &status);
   ASSERT_EQ(0, ret);
   ASSERT_EQ_FMT(NULL, status, "%p");

   ASSERT_EQ((void*)0x17, pthread_getspecific(mystr_key_2));
   ASSERT_EQ(pthread_key_delete(mystr_key_2), 0);
   ASSERT_EQ(0, pthread_getspecific(mystr_key_2));

   if (greatest_get_verbosity() != 0) {
      printf("joined %p, %d\n", (void*)pt2, ret);
   }

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args
   // greatest_set_verbosity(1);

   /* Tests can be run as suites, or directly. Lets run directly. */
   RUN_TEST(nested_threads);
   detached = 1;
   RUN_TEST(nested_threads);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}
