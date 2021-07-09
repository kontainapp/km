/*
 * Copyright 2021 Kontain Inc.
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

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

#include "greatest/greatest.h"

static const char* const brick_msg = "brick in the wall ";
static const char* const dust_msg = "one bites the dust";
const long step = 100 * 1024 * 1024 * 1024ul;

// simple thread create with check. assert macro can use params more than once, so using separate <ret>
#define MEM_THREAD(__id, __entry, __data)                                                          \
   {                                                                                               \
      int ret = pthread_create(&(__id), NULL, __entry, (void*)__data);                             \
      ASSERT_EQ(0, ret);                                                                           \
   }

#define MEM_JOIN(__id)                                                                             \
   {                                                                                               \
      void* thr_ret = NULL;                                                                        \
      int ret = pthread_join(__id, &thr_ret);                                                      \
      ASSERT_EQ(ret, 0);                                                                           \
      errors += (uint64_t)thr_ret;                                                                 \
   }

void* subrun(void* data)
{
   char* msg = (char*)data;
   uint64_t count = (msg == brick_msg ? step : step / 100);
   pthread_t self = pthread_self();
   int volatile n;

   for (long run_count = 0; run_count < count; run_count++) {
      if (run_count % step == 0) {
         // printf("Another %s # %ld of %ld, thr=0x%lx\n", msg, run_count / step, run_count, self);
         n = run_count / 100;
      }
   }
   printf("**** NOW IS THE END VCPU %d***\n", n);
   exit((uint64_t)self);
}

TEST intrtest(void)
{
   pthread_t pt1, pt2;
   int errors = 0;

   MEM_THREAD(pt1, subrun, dust_msg);
   MEM_THREAD(pt2, subrun, brick_msg);

   // printf("joining break threads \n");
   MEM_JOIN(pt1);
   MEM_JOIN(pt2);

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(intrtest);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
