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
 *
 * Basic test for mmap() and friends.
 */
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "greatest/greatest.h"

#include "km.h"
#include "km_mem.h"

// human readable print for addresses and sizes
static char* out_sz(uint64_t val)
{
   char* buf = malloc(64);   // yeah. leak it
   if (val < 1 * MIB) {
      sprintf(buf, "%ld", val);
      return buf;
   }
   if (val < 1 * GIB) {
      sprintf(buf, "%ldmb", val / MIB);
      return buf;
   }
   sprintf(buf, "%ldgb + %ldmb", val / GIB, val / MIB - (val / GIB) * 1024);
   return buf;
}

// positive tests
typedef struct mmap_test {
   char* test_info;        // string to help identify the test. NULL indicates the end of table
   int type;               // 1 for mmap, 0 for unmap
   uint64_t offset;        // for unmap, start offset from the last mmap result. For mmap: address
   size_t size;            // size for the operation
   int prot;               // protection for mmap()
   int flags;              // flags for mmap()
   int expected_failure;   // 0 if success is expected. Expected errno otherwise.
} mmap_test_t;

#define TYPE_MMAP 1
#define TYPE_MUNMAP 0

// After this set , the free/busy lists in mmaps should be empty and tbrk
// should reset to top of the VA

// tests that should pass on 36 bits buses (where we give 2GB space)
static mmap_test_t _36_tests[] = {
    //  {"Large-mmap2gb", TYPE_MMAP, 0, 2 * GIB + 12 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED |
    //  MAP_ANONYMOUS},
    //  {"Large-munmap2gb", TYPE_MUNMAP, 0, 2 * GIB + 12 * MIB, 0, 0},
    //  {"Large-mmap2", TYPE_MMAP, 0, 1 * GIB + 1020 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED |
    //  MAP_ANONYMOUS},
    //  {"Large-munmap2", TYPE_MUNMAP, 0, 1 * GIB + 1020 * MIB, 0, 0},

    // Dive into the bottom 1GB on 4GB:
    {"Large-mmap1.1", TYPE_MMAP, 0, 3 * GIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},

    {"Basic-mmap1", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Basic-munmap1", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {"Basic-mmap1", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Basic-munmap1", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {"Basic-mmap2", TYPE_MMAP, 0, 1020 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Basic-munmap2", TYPE_MUNMAP, 0, 1020 * MIB, 0, 0},
    {"Swiss cheese-mmap", TYPE_MMAP, 0, 760 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Swiss cheese-munmap1", TYPE_MUNMAP, 500 * MIB, 260 * MIB, 0, 0},
    {"Swiss cheese-munmap2", TYPE_MUNMAP, 0, 300 * MIB, 0, 0},
    {"Swiss cheese-munmap3", TYPE_MUNMAP, 300 * MIB, 200 * MIB, 0, 0},

    {"Wrong-args-mmap", TYPE_MMAP, 0x20000ul, 8 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, EINVAL},
    {"Wrong-args-munmap", TYPE_MUNMAP, 0x20000ul, 1 * MIB, 0, 0, EINVAL},
    {NULL},
};

// these tests will fail on 36 bit buses but should pass on larger address space
static mmap_test_t _39_tests[] = {
    {"Large-mmap1", TYPE_MMAP, 0, 1022 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Large-munmap1", TYPE_MUNMAP, 0, 1022 * MIB, 0, 0},
    {"Large-mmap2", TYPE_MMAP, 0, 2 * GIB + 12 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Large-munmap2", TYPE_MUNMAP, 0, 2 * GIB + 12 * MIB, 0, 0},
    {NULL},
};
// used for multiple invocations of mmap_test
static mmap_test_t* tests;

static const char* fmt = "0x%lx";   // format for offsets/types error msg

TEST mmap_test(void)
{
   void* last_addr = MAP_FAILED;   // changed by mmap; MAP_FAILED if mmap fails
   int ret;

   // positive tests
   for (mmap_test_t* t = tests; t->test_info != NULL; t++) {
      if (t->expected_failure != 0) {
         continue;
      }
      switch (t->type) {
         case TYPE_MMAP:
            printf("%s: mmap(%s, %s...)\n", t->test_info, out_sz(t->offset), out_sz(t->size));
            ASSERT_NOT_EQ_FMTm(t->test_info,
                               last_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0),
                               MAP_FAILED,
                               fmt);
            printf("Map OK, trying to memset '2' to 0x%lx size: 0x%lx\n", (uint64_t)last_addr, t->size);
            memset(last_addr, '2', t->size);
            break;
         case TYPE_MUNMAP:
            printf("%s: mumap(%s, %s...)\n",
                   t->test_info,
                   out_sz((km_gva_t)last_addr + t->offset),
                   out_sz(t->size));
            if (last_addr == MAP_FAILED) {
               printf("%s: Skipping munmap(MAP_FAILED, %s)\n", t->test_info, out_sz(t->size));
               break;
            }
            ASSERT_EQ_FMTm(t->test_info, ret = munmap(last_addr + t->offset, t->size), 0, fmt);
            break;
         default:
            ASSERT_EQ("Not reachable", NULL);
      }
   }

   // negative tests
   for (mmap_test_t* t = tests; t->test_info != NULL; t++) {
      if (t->expected_failure == 0) {
         continue;
      }
      switch (t->type) {
         case TYPE_MMAP:
            printf("%s: neg mmap(%s, %s...)\n", t->test_info, out_sz(t->offset), out_sz(t->size));
            ASSERT_EQ_FMTm(t->test_info,
                           mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0),
                           MAP_FAILED,
                           fmt);
            break;
         case TYPE_MUNMAP:
            printf("%s: neg mumap(%s, %s...)\n",
                   t->test_info,
                   out_sz((km_gva_t)last_addr + t->offset),
                   out_sz(t->size));
            ASSERT_EQ_FMTm(t->test_info, munmap(last_addr + t->offset, t->size), -1, fmt);
            break;
         default:
            ASSERT_EQ("Not reachable", NULL);
      }
   }
   PASS();
}

// test to make sure we can allocate from free() blocks
TEST mmap_from_free()
{
   void *addr, *addr1;

   /* 1. Allocate 200MB, cut a 100MB hole from a middle.
    * 2. Grab 100MB (should be allocated from the whole freed block), release it
    * 3. grab 50MB(should be allocated from the partial freed block), release it
    * 4. then release the rest */

   // 1.
   ASSERT_NOT_EQ(addr = mmap(0, 200 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0),
                 MAP_FAILED);
   ASSERT_EQ(munmap(addr + 50 * MIB, 100 * MIB), 0);

   // 2.
   ASSERT_NOT_EQ(addr1 = mmap(0, 100 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0),
                 MAP_FAILED);
   ASSERT_EQ(addr1, addr + 50 * MIB);
   ASSERT_EQ(munmap(addr1, 100 * MIB), 0);

   // 3.
   ASSERT_NOT_EQ(addr1 = mmap(0, 50 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0),
                 MAP_FAILED);
   ASSERT_EQ(addr1, addr + 50 * MIB);
   ASSERT_EQ(munmap(addr1, 50 * MIB), 0);

   ASSERT_NOT_EQ(addr1 = mmap(0, 100 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0),
                 MAP_FAILED);
   ASSERT_EQ(munmap(addr1, 100 * MIB), 0);

   // 4.
   ASSERT_EQ(munmap(addr, 50 * MIB), 0);
   ASSERT_EQ(munmap(addr + 150 * MIB, 50 * MIB), 0);

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   printf("Testing smaller (< 2GiB) sizes\n");
   tests = _36_tests;
   RUN_TEST(mmap_test);

   printf("Testing large (> 2GiB) sizes\n");
   tests = _39_tests;
   RUN_TEST(mmap_test);

   printf("Testing mmap() from free areas\n");
   RUN_TEST(mmap_from_free);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
