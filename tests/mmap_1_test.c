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
 * Test mmap/smaller mprotect in the middle/unmap sequence. Expect the maps clean after that.
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km_hcalls.h"
#include "syscall.h"

#include "mmap_test.h"

TEST mmap_overlap()
{
   static const size_t map_size = 1024 * 1024;
   static const size_t guard_size = 4 * 1024;

   if (greatest_get_verbosity() > 0) {
      printf("==== %s\n", __FUNCTION__);
   }
   void* s1 = mmap(0, map_size + 2 * guard_size, PROT_NONE, MAP_PRIVATE, -1, 0);
   ASSERT_EQ_FMT(MAP_FAILED, s1, "%p");   // no fds, no ANON and no FIXED should fail
   ASSERT_EQ_FMT(EBADF, errno, "%d");

   s1 = mmap(0, map_size + 2 * guard_size, PROT_NONE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, s1);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   mprotect(s1 + guard_size, map_size, PROT_READ | PROT_WRITE);
   munmap(s1, map_size + 2 * guard_size);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

TEST mmap_mprotect_overlap()
{
   if (greatest_get_verbosity() > 0) {
      printf("==== mmap_mprotect_overlap\n");
   }
   void* s1 = mmap(0, 1 * GIB, PROT_NONE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, s1);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);
   mprotect(s1 + 1 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);
   mprotect(s1 + 3 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);   // gap 1MB
   ASSERT_MMAPS_COUNT(7, TOTAL_MMAPS);
   mprotect(s1 + 2 * MIB, 2 * MIB, PROT_READ | PROT_WRITE);   // fill in the gap and some more
   mprotect(s1 + 2 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   munmap(s1, 1 * GIB);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

// Check MAP_FIXED  regions concat and splits
// Note: in KM we only allow it over existing regions
static const int rw = PROT_READ | PROT_WRITE;
static const size_t area_sz = 1 * GIB;
static const size_t offset1 = 100 * MIB;
static const size_t insert1_sz = 200 * MIB;

TEST mmap_fixed_basic()
{
   void *inside, *fixed;
   static const int rw = PROT_READ | PROT_WRITE;

   if (greatest_get_verbosity() > 0) {
      printf("==== %s: Concat fixed on the right\n", __FUNCTION__);
   }
   const size_t area_sz = 1 * GIB;
   const size_t offset1 = 100 * MIB;
   const size_t insert1_sz = 200 * MIB;

   // addr=0  MAP_FIXED should fail
   void* area = mmap(0, area_sz, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
   ASSERT_EQ(MAP_FAILED, area);
   ASSERT_EQ_FMT(EPERM, errno, "%d");

   area = mmap(0, area_sz, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, area);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   errno = 0;
   inside = area + offset1;
   fixed = mmap(inside, insert1_sz, rw, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
   ASSERT_EQ_FMT(inside, fixed, "%p");
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   inside = area + offset1 + insert1_sz;   // this goes over the end of existing range
   fixed = mmap(inside, area_sz - insert1_sz, rw, MAP_FIXED | MAP_PRIVATE, -1, 0);
   ASSERT_EQ(MAP_FAILED, fixed);
   ASSERT_EQ(EBADF, errno);

   inside = area + offset1 + insert1_sz;
   fixed = mmap(inside, area_sz - insert1_sz - offset1, rw, MAP_FIXED | MAP_PRIVATE, -1, 0);
   ASSERT_EQ(MAP_FAILED, fixed);   // should fail with errno=EBADF , fd<0 and no ANONYMOUS
   ASSERT_EQ(EBADF, errno);

   inside = area + offset1 + insert1_sz;
   fixed = mmap(inside, area_sz - insert1_sz - offset1, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ(inside, fixed);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);   // stack and our rw map should have merged

   munmap(area, area_sz);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

// similar to the above (1gb, cut a piece, add filler) but fillers are added on the left, and
// right, not to the edge.
TEST mmap_fixed_concat_both_sides()
{
   void *inside, *fixed, *area;

   if (greatest_get_verbosity() > 0) {
      printf("==== %s: Concat fixed on the both sides, but not to the full extent\n", __FUNCTION__);
   }
   area = mmap(0, area_sz, PROT_NONE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, area);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   inside = area + offset1;
   fixed = mmap(inside, insert1_sz, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ_FMT(inside, fixed, "%p");
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   inside = area + offset1 + insert1_sz;
   fixed = mmap(inside, area_sz - insert1_sz - offset1 - 10 * MIB, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ(inside, fixed);
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   fixed = mmap(area + 1 * MIB, offset1 - 1 * MIB, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ(area + 1 * MIB, fixed);
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   // fill the left gap
   fixed = mmap(area, 1 * MIB, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, fixed);
   ASSERT_MMAPS_COUNT(4, TOTAL_MMAPS);

   // fill the right gap - it should merge all with prior map
   fixed =
       mmap(area + offset1 + insert1_sz, area_sz - offset1 - insert1_sz, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, fixed);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);

   munmap(area, area_sz);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);

   PASS();
}

// fixed map over multiple regions
TEST mmap_fixed_over_multiple_regions()
{
   void *inside, *fixed, *area;
   if (greatest_get_verbosity() > 0) {
      printf("==== %s: fixed map over multiple regions\n", __FUNCTION__);
   }
   area = mmap(0, area_sz, PROT_NONE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, area);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   size_t offset2 = offset1 + insert1_sz + 10 * MIB;
   size_t insert2_sz = 1 * MIB;

   inside = area + offset1;
   fixed = mmap(inside, insert1_sz, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ_FMT(inside, fixed, "%p");
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   inside = area + offset2;
   fixed = mmap(inside, insert2_sz, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ_FMT(inside, fixed, "%p");
   ASSERT_MMAPS_COUNT(7, TOTAL_MMAPS);

   fixed = mmap(area + 10 * MIB, area_sz - 20 * MIB, rw, MAP_FIXED | flags, -1, 0);
   ASSERT_EQ_FMT(area + 10 * MIB, fixed, "%p");
   ASSERT_MMAPS_COUNT(5, TOTAL_MMAPS);

   munmap(area, area_sz);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

// mmap FIXED over unallocated works on Linux but should fail on KM
TEST mmap_fixed_incompat()
{
   void *area, *insert;

   if (greatest_get_verbosity() > 0) {
      printf("==== %s: fixed map should fail over unallocated \n", __FUNCTION__);
   }
   area = mmap(0, area_sz, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, area);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   // grabbing something before tbrk
   errno = 0;
   insert = mmap(area - insert1_sz, area_sz, rw, MAP_FIXED | flags, -1, 0);
   if (KM_PAYLOAD() == 1) {
      ASSERT_EQ_FMT(MAP_FAILED, insert, "%p");
      ASSERT_EQ_FMT(EINVAL, errno, "%d");
   } else {
      ASSERT_NOT_EQ(MAP_FAILED, insert);
   }

   // grabbing something too high, so it steps on Monitor reserved - KM only test
   if (KM_PAYLOAD() == 1) {
      errno = 0;
      insert = mmap(area, area_sz + insert1_sz, rw, MAP_FIXED | flags, -1, 0);
      ASSERT_EQ_FMT(MAP_FAILED, insert, "%p");
      ASSERT_EQ_FMT(EINVAL, errno, "%d");
   }

   // grabbing something with gaps
   int ret = munmap(area + 1 * MIB, 10 * MIB);
   ASSERT_EQ(0, ret);

   errno = 0;
   insert = mmap(area, area_sz - insert1_sz, rw, MAP_FIXED | flags, -1, 0);
   if (KM_PAYLOAD() == 1) {
      ASSERT_EQ_FMT(MAP_FAILED, insert, "%p");
      ASSERT_EQ_FMT(EINVAL, errno, "%d");
   } else {
      ASSERT_NOT_EQ(MAP_FAILED, insert);
   }

   munmap(area, area_sz);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(mmap_overlap);
   RUN_TEST(mmap_mprotect_overlap);
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   RUN_TEST(mmap_fixed_basic);
   RUN_TEST(mmap_fixed_concat_both_sides);
   RUN_TEST(mmap_fixed_over_multiple_regions);
   RUN_TEST(mmap_fixed_incompat);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
