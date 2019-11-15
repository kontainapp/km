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
 * Basic test for mmap() and friends.
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

#include "greatest/greatest.h"
#include "mmap_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../km/km_unittest.h"
#include "km_hcalls.h"
#include "syscall.h"

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

static const int MAX_MAPS = 4096;
static km_ut_get_mmaps_t* info;

static int get_maps(void)
{
   int ret;

   if (greatest_get_verbosity() <= 0) {
      return 0;
   }
   if (info == NULL) {
      info = calloc(1, sizeof(km_ut_get_mmaps_t) + MAX_MAPS * sizeof(km_mmap_reg_t));
      assert(info != NULL);
   }
   info->ntotal = MAX_MAPS;
   if ((ret = syscall(HC_km_unittest, KM_UT_GET_MMAPS_INFO, info)) == -EAGAIN) {
      printf("WOW, Km reported too many maps: %d", info->ntotal);
      return -1;
   }
   if (ret == -ENOTSUP) {
      return 0;   // silent skip
   }
   // printf("free %d busy %d\n", info->nfree, info->ntotal - info->nfree);
   assert(info->ntotal >= 1);   // we always have at least 1 mmap for stack
   size_t old_end = info->maps[0].start;
   for (km_mmap_reg_t* reg = info->maps; reg < info->maps + info->ntotal; reg++) {
      char* type = (reg < info->maps + info->nfree ? "free" : "busy");
      if (reg == info->maps + info->nfree) {   // reset distance on 'busy' list stat
         old_end = reg->start;
      }
      printf("mmap %s: 0x%lx size 0x%lx (%s) distance 0x%lx (%s), flags 0x%x prot 0x%x km_flags "
             "0x%x\n",
             type,
             reg->start,
             reg->size,
             out_sz(reg->size),
             reg->start - old_end,
             out_sz(reg->start - old_end),
             reg->flags,
             reg->protection,
             reg->km_flags.data32);
      old_end = reg->start + reg->size;
   }
   return 0;
}

// handler for SIGSEGV
static jmp_buf jbuf;
static int fail = 0;   // info from signal handled that something failed
void signal_handler(int signal)
{
   if (signal != SIGSEGV) {
      warn("Unexpected signal caught: %d", signal);
      fail = 1;
   }
   longjmp(jbuf, SIGSEGV);   // reusing SIGSEGV as setjmp() return
}

// positive tests
// After this set , the free/busy lists in mmaps should be empty and tbrk
// should reset to top of the VA

// tests that should pass on 36 bits buses (where we give 2GB space)
static mmap_test_t _36_tests[] = {
    // Dive into the bottom 1GB on 4GB:
    {__LINE__, "Basic-mmap1", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap1", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {__LINE__, "Basic-mmap1", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap1", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {__LINE__, "Basic-mmap2", TYPE_MMAP, 0, 1020 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap2", TYPE_MUNMAP, 0, 1020 * MIB, 0, 0},
    {__LINE__, "Swiss cheese-mmap", TYPE_MMAP, 0, 760 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Swiss cheese-munmap1", TYPE_MUNMAP, 500 * MIB, 260 * MIB, 0, 0},
    {__LINE__, "Swiss cheese-unaligned-munmap2", TYPE_MUNMAP, 0, 300 * MIB - 256, 0, 0},
    {__LINE__, "Swiss cheese-munmap3", TYPE_MUNMAP, 300 * MIB, 200 * MIB, 0, 0},

    {__LINE__, "simple map to set addr for test", TYPE_MMAP, 0, 1 * MIB, PROT_READ | PROT_WRITE, flags},
    // we ignore addr but it's legit to send it
    {__LINE__, "Wrong-args-mmap-addr", TYPE_MMAP, 400 * MIB, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Wrong-args-mmap-fixed", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags | MAP_FIXED, EINVAL},
    {__LINE__, "Wrong-args-munmap", TYPE_MUNMAP, 300 * MIB + 20, 1 * MIB, 0, 0, EINVAL},
    {__LINE__, "simple unmap to clean up ", TYPE_MUNMAP, 0, 1 * MIB, 0, 0},

    // it's legal to munmap non-mapped areas:
    {__LINE__, "huge-munmap", TYPE_MUNMAP, 300 * MIB, 1 * MIB, 0, 0, 0},
    {__LINE__, "dup-munmap", TYPE_MUNMAP, 300 * MIB, 8 * MIB, 0, 0, 0},
    {0, NULL},
};

// these tests will fail on 36 bit buses but should pass on larger address space
static mmap_test_t _39_tests[] = {
    {__LINE__, "Large-mmap2gb", TYPE_MMAP, 0, 2 * GIB + 12 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Large-munmap2gb", TYPE_MUNMAP, 0, 2 * GIB + 12 * MIB, 0, 0},
    {__LINE__, "Large-mmap1", TYPE_MMAP, 0, 1022 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Large-munmap1", TYPE_MUNMAP, 0, 1022 * MIB, 0, 0},
    {__LINE__, "Large-mmap2", TYPE_MMAP, 0, 2 * GIB + 12 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Large-munmap2", TYPE_MUNMAP, 0, 2 * GIB + 12 * MIB, 0, 0},
    {__LINE__, "Large-mmap2.1020", TYPE_MMAP, 0, 1 * GIB + 1020 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Large-munmap2.1020", TYPE_MUNMAP, 0, 1 * GIB + 1020 * MIB, 0, 0},
    {__LINE__, "Large-mmap3GB", TYPE_MMAP, 0, 3 * GIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Large-unmmap3GB", TYPE_MUNMAP, 0, 3 * GIB, 0, 0},
    {0, NULL},
};
// used for multiple invocations of mmap_test
static mmap_test_t* tests;

// generic test, expects global 'tests' to be setup to point to the right test data table
TEST mmap_test(void)
{
   int ret;
   void* remapped_addr;            // address mremap uses (usually last_addr + offset)
   void* new_addr = MAP_FAILED;    // address mremap on map returns.
   void* last_addr = MAP_FAILED;   // changed by successful mmap

   for (mmap_test_t* t = tests; t->info != NULL; t++) {
      errno = 0;
      if (greatest_get_verbosity() == 1) {
         printf("* %s: last_addr %p, offset 0x%lx (%s) size 0x%lx (%s)\n",
                t->info,
                last_addr,
                t->offset,
                out_sz(t->offset),
                t->size,
                out_sz(t->size));
      }
      switch (t->type) {
         case TYPE_MMAP:
            new_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0);
            if (greatest_get_verbosity() == 1) {
               printf("return: %p (%s)\n", new_addr, out_sz((uint64_t)new_addr));
            }
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               if ((t->prot & PROT_READ) != 0) {
                  ASSERT_EQm("Mmaped memory should be zeroed", 0, *(int*)new_addr);
               }
               if ((t->prot & PROT_WRITE) != 0) {
                  if (greatest_get_verbosity() == 1) {
                     printf("Mmap OK, trying to memset '2' to 0x%lx size: 0x%lx (%s)\n",
                            (uint64_t)new_addr,
                            t->size,
                            out_sz(t->size));
                  }
                  memset(new_addr, '2', t->size);
               }
               last_addr = new_addr;
            } else {
               ASSERT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            get_maps();
            break;
         case TYPE_MUNMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = munmap(last_addr + t->offset, t->size);
            get_maps();
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_MREMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            remapped_addr = last_addr + t->offset;
            size_t old_size = t->size;
            size_t new_size = t->prot;
            new_addr = mremap(remapped_addr, old_size, new_size, t->flags);
            get_maps();
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               if (greatest_get_verbosity() == 1) {
                  printf("mremap OK %p -> %p, will memset '2' size old 0x%lx (%s) new 0x%lx (%s)\n",
                         last_addr,
                         new_addr,
                         old_size,
                         out_sz(old_size),
                         new_size,
                         out_sz(new_size));
               }
               if (old_size < new_size) {   // WE ASSUME PROT_READ for the parent map !
                  printf("VALUE %d at %p\n", *(int*)(new_addr + old_size), new_addr);
                  ASSERT_EQm("new range in remap should be zeroed", 0, *(int*)(new_addr + old_size));
               }
               memset(new_addr, '2', new_size);   // just core dumps if something is wrong
               signal(SIGSEGV, signal_handler);
               if ((ret = setjmp(jbuf)) == 0) {
                  if (new_addr != remapped_addr) {   // old memory should be not accessible now
                     memset(remapped_addr, '2', old_size);
                     FAILm("memset to new address is successful and should be not");
                  } else if (old_size > new_size) {   // shrinking. Extra should be unmapped by now
                     ASSERT_EQ(new_addr, remapped_addr);
                     void* unmapped_addr = remapped_addr + new_size;
                     size_t unmapped_size = old_size - new_size;
                     memset(unmapped_addr, '2', unmapped_size);
                     printf("memset to removed %p size 0x%lx (%s) should have failed but did not\n",
                            unmapped_addr,
                            unmapped_size,
                            out_sz(unmapped_size));
                     FAIL();
                  }
               } else {
                  assert(ret == SIGSEGV);   // we use that value in longjmp
               }
               signal(t->expected, SIG_DFL);
            } else {   // expecting failure
               ASSERT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_USE_MREMAP_ADDR:
            assert(new_addr != MAP_FAILED);
            last_addr = new_addr;
            break;
         case TYPE_MPROTECT:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = mprotect(last_addr + t->offset, t->size, t->prot);
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            get_maps();
            break;
         case TYPE_WRITE:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               break;
            }
            signal(t->expected, signal_handler);
            if ((ret = setjmp(jbuf)) == 0) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               printf("Write to %p (sz 0x%lx) was successful and should be not (line %d)\n",
                      last_addr + t->offset,
                      t->size,
                      t->line);
               FAIL();
            } else {
               assert(ret == SIGSEGV);   // we use that value in longjmp
            }
            signal(t->expected, SIG_DFL);
            ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            break;
         case TYPE_READ:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }
               break;
               signal(t->expected, signal_handler);
               if ((ret = setjmp(jbuf)) == 0) {
                  char ch_expected = t->prot;
                  for (size_t i = 0; i < t->size; i++) {
                     volatile char c = *(char*)(last_addr + t->offset + i);
                     if (ch_expected != 0) {
                        ASSERT_EQm("Reading comparison failed", ch_expected, c);
                     }
                  }
                  FAILm("Read successful and should be not");   // return
               }
               assert(ret == SIGSEGV);   // we use that value in longjmp
               signal(t->expected, SIG_DFL);
               ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            }
            break;
         default:
            ASSERT_EQ(NULL, "Not reachable");
      }   // switch
   }      // for
   PASS();
}

TEST mmap_test_36(void)
{
   printf("===== mmap_test_36: Testing smaller (< 2GiB) sizes\n");
   tests = _36_tests;
   CHECK_CALL(mmap_test());
   PASS();
}

TEST mmap_test_39(void)
{
   printf("===== mmap_test_39: Testing large (> 2GiB) sizes\n");
   tests = _39_tests;
   CHECK_CALL(mmap_test());
   PASS();
}

// test to make sure we can allocate from free() blocks
TEST mmap_from_free()
{
   void *addr, *addr1, *addr2;
   int ret;

   printf("===== mmap_from_free: Testing mmap() from free areas\n");
   /* 1. Allocate 200MB, allocate another 10MB to block tbrk change,  cut a 100MB hole from a
    * middle of first map.
    * 2. Grab 100MB (should be allocated from the whole freed block), release it
    * 3. grab 50MB(should be allocated from the partial freed block), release it
    * 4. then release the rest */

   // 1.
   addr = mmap(0, 200 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ_FMT(MAP_FAILED, addr, "%p");
   addr2 = mmap(0, 10 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ_FMT(MAP_FAILED, addr2, "%p");
   ret = munmap(addr + 50 * MIB, 100 * MIB);
   ASSERT_EQ_FMT(0, ret, "%d");

   // 2.
   addr1 = mmap(0, 100 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ_FMT(MAP_FAILED, addr1, "%p");

   ASSERT_EQ_FMTm("mmap to the free carved", addr + 50 * MIB, addr1, "%p");
   ret = munmap(addr1, 100 * MIB);
   ASSERT_EQ_FMTm("Unmap from the middle", 0, ret, "%d");

   // 3.
   addr1 = mmap(0, 50 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, addr1);
   ASSERT_EQ(addr + 50 * MIB, addr1);
   ret = munmap(addr1, 50 * MIB);
   ASSERT_EQ(0, ret);

   addr1 = mmap(0, 100 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, addr1);
   ASSERT_EQ(0, munmap(addr1, 100 * MIB));

   // 4.
   ret = munmap(addr2, 10 * MIB);
   ASSERT_EQ(0, ret);
   ret = munmap(addr, 50 * MIB);
   ASSERT_EQ(0, ret);
   ret = munmap(addr + 150 * MIB, 50 * MIB);
   ASSERT_EQ(0, ret);

   PASS();
}

// test that (1) munmapped memory is access protected (2) we honor mmap prot flag
TEST mmap_protect()
{
   void *addr, *addr1;
   int ret;

   printf("===== mmap_protect: Testing protection for unmapped area\n");
   signal(SIGSEGV, signal_handler);
   // get mmap, carve 1MIB munmapped section and try to access it. Should call the handler
   addr = mmap(0, 200 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ_FMT(MAP_FAILED, addr, "%p");
   addr1 = addr + 10 * MIB;
   ASSERT_EQ_FMTm("Unmap from the middle", 0, munmap(addr1, 1 * MIB), "%d");
   if ((ret = setjmp(jbuf)) == 0) {
      strcpy((char*)addr1, "writing to unmapped area");
      FAILm("Write successful and should be not");
   } else {
      ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
      printf("Handled SIGSEGV on unmmaped mem successfully... continuing...\n");
   }
   // now lets' check we respect PROT flags
   void* mapped = mmap(0, 1 * MIB, PROT_READ, flags, -1, 0);
   ASSERT_EQ_FMT(addr1, mapped, "%p");   // we expect to grab the btarea just released
   uint8_t buf[1024];
   memcpy(buf, (uint8_t*)addr1, sizeof(buf));   // read should succeed
   if ((ret = setjmp(jbuf)) == 0) {
      strcpy((char*)addr1, "writing to write-protected area");   // write should fail
      FAILm("Write successful and should be not");
   } else {
      ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
      printf("Handled SIGSEGV on write-protected mem violation... continuing...\n");
   }

   // now unmap the leftovers
   ASSERT_EQ_FMTm("Unmap head", 0, munmap(addr, 10 * MIB), "%d");
   ASSERT_EQ_FMTm("Unmap from the middle again", 0, munmap(addr1, 1 * MIB), "%d");
   ASSERT_EQ_FMTm("Unmap tail", 0, munmap(addr1 + 1 * MIB, 200 * MIB - 10 * MIB - MIB), "%d");

   signal(SIGSEGV, SIG_DFL);
   PASS();
}

// helper to test glue
TEST mremap_test()
{
   static const int prot = (PROT_READ | PROT_WRITE);
   static mmap_test_t mremap_tests[] = {
       {__LINE__, "1 mmap", TYPE_MMAP, 0, 10 * MIB, prot, flags, OK},

       {__LINE__, "1f1 mremap param - FIXED flag", TYPE_MREMAP, 0, 2 * MIB, 1 * MIB, MREMAP_FIXED, EINVAL},
       {__LINE__, "1f2 mremap param - new_size 0", TYPE_MREMAP, 0, 1 * MIB, 0 * MIB, MREMAP_MAYMOVE, EINVAL},
       {__LINE__, "1f3 mremap param - size 0", TYPE_MREMAP, 0, 0 * MIB, 1 * MIB, MREMAP_MAYMOVE, EINVAL},
       {__LINE__, "1f4 mremap param - unaligned", TYPE_MREMAP, 1, 2 * MIB, 1 * MIB, MREMAP_MAYMOVE, EINVAL},
       {__LINE__, "1f5 mremap param - wrong flags", TYPE_MREMAP, 0, 2 * MIB, 1 * MIB, 0x44, EINVAL},

       {__LINE__, "1 mremap shrink makes 1mb hole", TYPE_MREMAP, 0, 2 * MIB, 1 * MIB, MREMAP_MAYMOVE, OK},
       {__LINE__, "1 mmap refill the hole", TYPE_MMAP, 1 * MIB, 1 * MIB, prot, flags, OK},
       {__LINE__, "1 cleanup (unmap)", TYPE_MUNMAP, -1 * MIB, 10 * MIB, PROT_NONE, flags, OK},   //// BUG

       // grow should move ptr; old area should be unaccessible.
       {__LINE__, "2 mmap", TYPE_MMAP, 0, 2 * MIB, prot, flags, OK},
       {__LINE__, "2 write", TYPE_WRITE, 0, 1 * KM_PAGE_SIZE, '2', 0, OK},
       {__LINE__, "2 mremap grow", TYPE_MREMAP, 0, 2 * MIB, 3 * MIB, MREMAP_MAYMOVE, OK},
       {__LINE__, "2 write old should SIGSEGV", TYPE_WRITE, 0, 1 * KM_PAGE_SIZE, '?', 0, SIGSEGV},   // old area gone
       {__LINE__, "2 switch last_addr to mremap", TYPE_USE_MREMAP_ADDR},
       {__LINE__, "2 read new", TYPE_READ, 0, 1 * KM_PAGE_SIZE, '2', 0, OK},
       {__LINE__, "2 write new tail", TYPE_WRITE, 03 * MIB - 1 * KM_PAGE_SIZE, 1 * KM_PAGE_SIZE, '?', 0, OK},

       {__LINE__, "2 cleanup (unmap)", TYPE_MUNMAP, 0, 3 * MIB, PROT_NONE, flags, OK},

       // TBD
       {__LINE__, "3 mmap", TYPE_MMAP, 0, 10 * MIB, prot, flags, OK},

       {__LINE__, "3 munmap make a hole", TYPE_MUNMAP, 2 * MIB, 1 * MIB, PROT_NONE, flags, OK},   // hole 2mb->3mb
       {__LINE__, "3 mremap grow", TYPE_MREMAP, 1 * MIB, 1 * MIB, 2 * MIB - 2 * KM_PAGE_SIZE, MREMAP_MAYMOVE, OK},
       {__LINE__, "3 write to remapped", TYPE_WRITE, 2 * MIB + 2 * KM_PAGE_SIZE, 1 * KM_PAGE_SIZE, '2', 0, OK},
       {__LINE__, "3 wr free -> SIGSEGV", TYPE_WRITE, 3 * MIB - 1 * KM_PAGE_SIZE, 1 * KM_PAGE_SIZE, '?', 0, SIGSEGV},
       {__LINE__, "3 mremap grow plug", TYPE_MREMAP, 1 * MIB, 2 * MIB - 2 * KM_PAGE_SIZE, 2 * MIB, MREMAP_MAYMOVE, OK},
       {__LINE__, "3 WR last pg -> Ok", TYPE_WRITE, 3 * MIB - 1 * KM_PAGE_SIZE, 1 * KM_PAGE_SIZE, '?', 0, OK},

       {__LINE__, "3 cleanup (unmap)", TYPE_MUNMAP, 0, 10 * MIB, PROT_NONE, flags, OK},

       // TBD - failure to remap over different mmaps
       //  {__LINE__, "4. mprotect PROT_READ", TYPE_MPROTECT, 8 * MIB, 10 * MIB, PROT_READ,
       //  flags, OK},
       //  {__LINE__, "4f mremap grow over protected", TYPE_MREMAP, 0, 10 * MIB, 12 * MIB,
       //  MREMAP_MAYMOVE, EFAULT},

       {0, NULL},
   };

   printf("===== mremap: Testing mremap() functionality\n");
   tests = mremap_tests;
   get_maps();
   CHECK_CALL(mmap_test());
   PASS();
}

TEST mmap_file_test()
{
   printf("===== mmap_file: Testing mmap() with file\n");
   static char fname[] = "/tmp/mmap_test_XXXXXX";
   int fd = mkstemp(fname);
   ASSERT_NOT_EQ(-1, fd);

   int nbufs = 10;
   char buffer[4096];
   for (int i = 0; i < nbufs; i++) {
      memset(buffer, 'a' + i, sizeof(buffer));
      ASSERT_EQ(sizeof(buffer), write(fd, buffer, sizeof(buffer)));
   }

   // mmap whole file.
   void* m = mmap(NULL, nbufs * sizeof(buffer), PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
   ASSERT_NOT_EQ((void*)-1, m);

   // Check contents
   char* s = m;
   for (int i = 0; i < nbufs; i++) {
      ASSERT_EQ('a' + i, s[sizeof(buffer) * i]);
   }

   // Map contents of page[2] on page[1].
   void* t =
       mmap(s + sizeof(buffer), sizeof(buffer), PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, sizeof(buffer) * 2);
   ASSERT_NOT_EQ(MAP_FAILED, t);
   ASSERT_EQ(s + sizeof(buffer), t);
   ASSERT_EQ('a' + 2, s[sizeof(buffer)]);
   ASSERT_EQ(0, munmap(m, nbufs * sizeof(buffer)));
   close(fd);
   unlink(fname);
   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(mmap_test_36);
   RUN_TEST(mmap_test_39);
   RUN_TEST(mmap_from_free);
   RUN_TEST(mmap_protect);
   RUN_TEST(mremap_test);
   RUN_TEST(mmap_file_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
