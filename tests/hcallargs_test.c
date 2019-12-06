/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * The point of this test is to make sure hypercall args are passed correctly even when thread stack
 * is far from the top of memory. The code in hypercall() in km_vcpu_run() makes an assumption that
 * hypercall args are on the stack and stack isn't larger than 4GiB. In many cases the whole memory
 * is less than 4GB sp it's hard to convince ourselves that code works. So we inteleave multiple
 * large mmaps and short lived threads.
 */
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "greatest/greatest.h"
#include "sys/mman.h"
#include "syscall.h"

#define GIB (1024 * 1024 * 1024ul)

static const long sizes[] = {3 * GIB, 4 * GIB, 5 * GIB, 6 * GIB};
const long size_cnt = sizeof(sizes) / sizeof(long);

static inline char* simple_mmap(size_t size)
{
   // Since we don't check memory content and get huge maps, use PROT_NONE to avoid memset() in km_mmaps.c
   return mmap(0, size, PROT_NONE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

static const char* brick_msg = "brick in the wall ";
static const char* dust_msg = "one bites the dust";
const long step = 100ul;

void* run(void* unused)
{
   pthread_detach(pthread_self());
   for (long run_count = 0; run_count < 2; run_count++) {
      printf("Another %s, %ld\n",
             run_count % 2 == 0 ? brick_msg : dust_msg,
             (0x800000000000 - (uint64_t)&run_count) / (4 * GIB));
   }
   return 0;
}

// simple thread create with check. assert macro can use params more than once, so using separate <ret>
#define MEM_THREAD(__id, __entry)                                                                  \
   {                                                                                               \
      (void)pthread_create(&(__id), NULL, __entry, NULL);                                          \
      printf("started %s 0x%lx\n", #__id, __id);                                                   \
   }

void* run_mmap(void* unused)
{
   void* ret[step];

   pthread_t pt;
   for (int i = 0; i < step; i++) {
      if ((ret[i] = simple_mmap(sizes[i % size_cnt])) == MAP_FAILED) {
         return (void*)1;
      }
      MEM_THREAD(pt, run);
   }
   for (int i = 0; i < step; i += 2) {
      munmap(ret[i], sizes[i % size_cnt]);
      MEM_THREAD(pt, run);
   }
   for (int i = 1; i < step; i += 2) {
      munmap(ret[i], sizes[i % size_cnt]);
      MEM_THREAD(pt, run);
   }
   return NULL;
}

#define MEM_JOIN(__id)                                                                             \
   {                                                                                               \
      void* thr_ret = NULL;                                                                        \
      int ret = pthread_join(__id, &thr_ret);                                                      \
      printf("joined %s 0x%lx ret=%d err_count=%ld\n", #__id, __id, ret, (uint64_t)thr_ret);       \
      ASSERT_EQ(ret, 0);                                                                           \
      errors += (uint64_t)thr_ret;                                                                 \
   }

TEST nested_threads(void)
{
   pthread_t pt_m;
   uint64_t errors = 0;   // will be changed by macro below

   MEM_THREAD(pt_m, run_mmap);
   MEM_JOIN(pt_m);

   ASSERT_EQ(0, errors);
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

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}