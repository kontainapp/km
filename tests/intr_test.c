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
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

#include "greatest/greatest.h"

static const char* brick_msg = "brick in the wall ";
static const char* dust_msg = "one bites the dust";
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
   exit(self);
}

TEST intrtest(void)
{
   pthread_t pt1, pt2;
   int errors;

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