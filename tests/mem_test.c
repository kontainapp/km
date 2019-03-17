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
 * Check multiple simultaneous brk and mmap operation taking and releasing memory. The point is to
 * test mutual exclusion between them.
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

#define MIB (1024 * 1024)

static const long sizes[] = {32 * MIB, 64 * MIB, 128 * MIB, 256 * MIB, 512 * MIB, 1024 * MIB};
const long step = sizeof(sizes) / sizeof(long);

static inline char* simple_mmap(size_t size)
{
   return mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

void* subrun(void* up)
{
   char* array[step];
   int i;
   uint64_t errors = 0;

   if (up != NULL) {
      for (i = 0; i < step; i++) {
         if ((array[i] = simple_mmap(sizes[i])) == MAP_FAILED) {
            errors++;
         }
      }
      for (i = 0; i < step; i++) {
         if (array[i] != MAP_FAILED) {
            munmap(array[i], sizes[i]);
         }
      }
   } else {
      for (i = step - 1; i >= 0; i--) {
         if ((array[i] = simple_mmap(sizes[i])) == MAP_FAILED) {
            errors++;
         }
      }
      for (i = step - 1; i >= 0; i--) {
         if (array[i] != MAP_FAILED) {
            munmap(array[i], sizes[i]);
         }
      }
   }
   pthread_exit((void*)errors);
}

void* run(void* unused)
{
   uint64_t errors = 0;
   void* thr_ret;

   for (long run_count = 0; run_count < 128; run_count++) {
      pthread_t pt1, pt2;

      pthread_create(&pt1, NULL, subrun, NULL);
      pthread_create(&pt2, NULL, subrun, (void*)1);
      pthread_join(pt2, &thr_ret);
      errors += *(uint64_t*)thr_ret;
      pthread_join(pt1, &thr_ret);
      errors += *(uint64_t*)thr_ret;
   }
   return (void*)errors;
}

static inline void* SYS_break(void const* addr)
{
   return (void*)syscall(SYS_brk, addr);
}

void* run_brk(void* unused)
{
   void* start_brk = SYS_break(0);
   void* ptr;
   void* ret;

   for (int cnt = 0; cnt < 1000; cnt++) {
      ptr = start_brk;
      for (int i = 0; i < step; i++) {
         ptr += sizes[i];
         ret = SYS_break(ptr);
         if (ret == (void*)-1 || ptr != SYS_break(0)) {
            printf("break L%d: ptr=%p br=%p ret=%p errno=%d\n", __LINE__, ptr, SYS_break(0), ret, errno);
            return (void*)1;
         }
      }
      for (int i = 0; i < step; i++) {
         ptr -= sizes[i];
         ret = SYS_break(ptr);
         if (ret == (void*)-1 || ptr != SYS_break(0)) {
            printf("break L%d: ptr=%p br=%p ret=%p errno=%d\n", __LINE__, ptr, SYS_break(0), ret, errno);
            return (void*)1;
         }
      }
   }
   printf("======================== Ending the brk thread ========================\n");
   return NULL;
}

TEST nested_threads(void)
{
   pthread_t pt1, pt2, pt_b1, pt_b2;
   int ret;
   void* thr_ret = NULL;
   uint64_t errors = 0;

   // assert macro can use params more than once, so using separate <ret>
   ret = pthread_create(&pt_b1, NULL, run_brk, NULL);
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt_b1);

   ret = pthread_create(&pt_b2, NULL, run_brk, NULL);
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt_b2);

   ret = pthread_create(&pt1, NULL, run, NULL);
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt1);

   ret = pthread_create(&pt2, NULL, run, NULL);
   ASSERT_EQ(0, ret);
   printf("started 0x%lx\n", pt2);

   printf("joining break threads \n");

   ret = pthread_join(pt_b1, &thr_ret);
   printf("joined 0x%lx ret=%d %ld\n", pt_b1, ret, (long int)thr_ret);
   ASSERT_EQ(ret, 0);
   errors += *(uint64_t*)thr_ret;

   printf("ready to join next\n");
   ret = pthread_join(pt_b2, &thr_ret);
   printf("joined 0x%lx ret=%d %ld\n", pt_b2, ret, (long int)thr_ret);
   ASSERT_EQ(ret, 0);
   errors += *(uint64_t*)thr_ret;

   printf("joining 0x%lx ... \n", pt1);
   ret = pthread_join(pt1, &thr_ret);
   printf("joined 0x%lx, ret=%d %ld\n", pt1, ret, (long int)thr_ret);
   ASSERT_EQ(ret, 0);
   errors += *(uint64_t*)thr_ret;

   printf("joining 0x%lx ... \n", pt2);
   printf("joined 0x%lx, %d\n", pt2, ret = pthread_join(pt2, &thr_ret));
   ASSERT_EQ(ret, 0);
   errors += *(uint64_t*)thr_ret;

   ASSERT_EQ(errors, 0);
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

   GREATEST_MAIN_END();          // display results
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}