/*
 * Copyright © 2020-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * measure time for dummy hypercall
 * measure time for pagefault
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "greatest/greatest.h"

/*
 * getuid boils down to a dummy call currently
 * use it to measure rtt
 */
#define TEST_MAX_TIME_DUMMY_HYPERCALL (1)
#define TEST_MAX_TIME_PAGE_FAULT (1)
#define LOOP_COUNT (1 * 1024)
#define TEST_PAGE_SIZE (4096ULL)
#define NSEC_PER_SEC (1000 * 1000 * 1000ULL)
#define NSEC_PER_MILLI_SEC (1000 * 1000ULL)

    typedef struct {
   struct timespec start;
   struct timespec end;
   u_int64_t nsec_consumed;
   char buffer[1024];
} sample_t;

sample_t sample;

void show_time(char* name, sample_t* s)
{
   u_int64_t start_nsec, end_nsec;

   start_nsec = sample.start.tv_sec * NSEC_PER_SEC + sample.start.tv_nsec;
   end_nsec = sample.end.tv_sec * NSEC_PER_SEC + sample.end.tv_nsec;
   s->nsec_consumed = end_nsec - start_nsec;

   snprintf(s->buffer,
            sizeof(s->buffer),
            "\n\t%s sec:mil-sec\n"
            "\t\tbegin time %ld:%03lld\n"
            "\t\tend time   %ld:%03lld\n"
            "\t\t\ttime %lld:%03lld\n",
            name,
            sample.start.tv_sec,
            sample.start.tv_nsec / NSEC_PER_MILLI_SEC,
            sample.end.tv_sec,
            sample.end.tv_nsec / NSEC_PER_MILLI_SEC,
            s->nsec_consumed / NSEC_PER_SEC,
            (s->nsec_consumed % NSEC_PER_SEC) / NSEC_PER_MILLI_SEC);
}

TEST hypercall_time(sample_t* s)
{
   int ret_val = 0;
   volatile uid_t uid = 0;

   ret_val = clock_gettime(CLOCK_MONOTONIC_RAW, &s->start);
   ASSERT_NOT_EQm("clock_gettime failed\n", ret_val, -1);

   for (int i = 0; i < LOOP_COUNT; i++) {
      uid = getuid();
      (void)uid;
   }

   ret_val = clock_gettime(CLOCK_MONOTONIC_RAW, &s->end);
   ASSERT_NOT_EQm("clock_gettime failed\n", ret_val, -1);

   show_time("hcall", &sample);
   ASSERTm("test took too long\n", (s->nsec_consumed / NSEC_PER_SEC) < TEST_MAX_TIME_DUMMY_HYPERCALL);

   PASSm(s->buffer);
}

TEST mmap_time(sample_t* s, bool write)
{
   int ret_val = 0;
   volatile char* addr = NULL;
   volatile char value = 0;
   size_t map_size = TEST_PAGE_SIZE * LOOP_COUNT;

   addr = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   ASSERTm("mmap failed\n", addr != MAP_FAILED);

   ret_val = clock_gettime(CLOCK_MONOTONIC_RAW, &s->start);
   ASSERT_NOT_EQm("clock_gettime failed\n", ret_val, -1);

   for (int i = 0; i < LOOP_COUNT; i++) {
      if (write == true) {
         *(addr + TEST_PAGE_SIZE * i) = value;
      } else {
         value = *(addr + TEST_PAGE_SIZE * i);
      }
   }

   ret_val = munmap((void*)addr, map_size);
   ASSERTm("munmap failed", ret_val == 0);

   ret_val = clock_gettime(CLOCK_MONOTONIC_RAW, &s->end);
   ASSERT_NOT_EQm("clock_gettime failed %s\n", ret_val, -1);

   show_time((write == false) ? "mmap read" : "mmap write", &sample);
   ASSERTm("test took too long\n", (s->nsec_consumed / NSEC_PER_SEC) < TEST_MAX_TIME_PAGE_FAULT);

   PASSm(s->buffer);
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TESTp(hypercall_time, &sample);

   RUN_TESTp(mmap_time, &sample, false);

   RUN_TESTp(mmap_time, &sample, true);

   GREATEST_MAIN_END();
}
