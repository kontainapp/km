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
   // Since we don't check memory content and get huge maps, use PROT_NONE to avoid memset() in km_mmaps.c
   return mmap(0, size, PROT_NONE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
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

   // printf("starting run\n");
   for (long run_count = 0; run_count < 128; run_count++) {
      pthread_t pt1, pt2;

      if (pthread_create(&pt1, NULL, subrun, NULL) != 0) {
         errors++;
      }
      if (pthread_create(&pt2, NULL, subrun, (void*)1) != 0) {
         errors++;
      }
      if (pthread_join(pt2, &thr_ret) != 0) {
         errors++;
      } else {
         errors += (uint64_t)thr_ret;
      }
      if (pthread_join(pt1, &thr_ret) != 0) {
         errors++;
      } else {
         errors += (uint64_t)thr_ret;
      }
   }
   printf("run errors = %ld\n", errors);
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
         if (ret == (void*)-1) {
            printf("break up: L%d: ptr=%p br=%p errno=%d size=0x%lx (i=%d)\n",
                   __LINE__,
                   ptr,
                   SYS_break(0),
                   errno,
                   sizes[i],
                   i);
            SYS_break(start_brk);
            return (void*)1;
         }
      }
      for (int i = 0; i < step; i++) {
         ptr -= sizes[i];
         ret = SYS_break(ptr);
         if (ret == (void*)-1) {
            printf("break down: L%d: ptr=%p br=%p errno=%d size=0x%lx (i=%d)\n",
                   __LINE__,
                   ptr,
                   SYS_break(0),
                   errno,
                   sizes[i],
                   i);
            SYS_break(start_brk);
            return (void*)1;
         }
      }
   }
   printf("======================== Ending the brk thread ========================\n");
   return NULL;
}

// simple thread create with check. assert macro can use params more than once, so using separate <ret>
#define MEM_THREAD(__id, __entry)                                                                  \
   {                                                                                               \
      int ret = pthread_create(&(__id), NULL, __entry, NULL);                                      \
      ASSERT_EQ(0, ret);                                                                           \
      printf("started %s 0x%lx\n", #__id, __id);                                                   \
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
   pthread_t pt1, pt2, pt_b1, pt_b2;
   uint64_t errors = 0;   // will be changed by macro below

   MEM_THREAD(pt_b1, run_brk);
   MEM_THREAD(pt_b2, run_brk);
   MEM_THREAD(pt1, run);
   MEM_THREAD(pt2, run);
   printf("joining break threads \n");
   MEM_JOIN(pt_b1);
   MEM_JOIN(pt_b2);
   printf("joining mmap threads \n");
   MEM_JOIN(pt1);
   MEM_JOIN(pt2);

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