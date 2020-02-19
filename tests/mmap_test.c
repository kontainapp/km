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

#include "km_hcalls.h"
#include "syscall.h"

#include "mmap_test.h"

// positive tests
// After this set , the free/busy lists in mmaps should be empty and tbrk
// should reset to top of the VA

// tests that should pass on 36 bits buses (where we give 2GB space)
static mmap_test_t _36_tests[] = {
    // Dive into the bottom 1GB on 4GB:
    {__LINE__, "Basic-mmap1", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap1", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {__LINE__, "Basic-mmap2", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap2", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {__LINE__, "Basic-mmap3", TYPE_MMAP, 0, 1020 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Basic-munmap3", TYPE_MUNMAP, 0, 1020 * MIB, 0, 0},
    {__LINE__, "Swiss cheese-mmap", TYPE_MMAP, 0, 760 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Swiss cheese-munmap1", TYPE_MUNMAP, 500 * MIB, 260 * MIB, 0, 0},
    {__LINE__, "Swiss cheese-unaligned-munmap2", TYPE_MUNMAP, 0, 300 * MIB - 256, 0, 0},
    {__LINE__, "Swiss cheese-munmap3", TYPE_MUNMAP, 300 * MIB, 200 * MIB, 0, 0},

    {__LINE__, "simple map to set addr for test", TYPE_MMAP, 0, 1 * MIB, PROT_READ | PROT_WRITE, flags},
    // we ignore addr but it's legit to send it
    {__LINE__, "Wrong-args-mmap-addr", TYPE_MMAP, 400 * MIB, 8 * MIB, PROT_READ | PROT_WRITE, flags},
    {__LINE__, "Wrong-args-mmap-fixed", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, flags | MAP_FIXED, EPERM},
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

TEST mmap_test_36(void)
{
   printf("===== mmap_test_36: Testing smaller (< 2GiB) sizes\n");
   tests = _36_tests;
   CHECK_CALL(mmap_test(tests));
   PASS();
}

TEST mmap_test_39(void)
{
   printf("===== mmap_test_39: Testing large (> 2GiB) sizes\n");
   tests = _39_tests;
   CHECK_CALL(mmap_test(tests));
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
   signal(SIGSEGV, sig_handler);
   // get mmap, carve 1MIB munmapped section and try to access it. Should call the handler
   addr = mmap(0, 200 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
   ASSERT_NOT_EQ_FMT(MAP_FAILED, addr, "%p");
   addr1 = addr + 10 * MIB;
   ASSERT_EQ_FMTm("Unmap from the middle", 0, munmap(addr1, 1 * MIB), "%d");
   if ((ret = sigsetjmp(jbuf, 1)) == 0) {
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
   if ((ret = sigsetjmp(jbuf, 1)) == 0) {
      strcpy((char*)addr1, "writing to write-protected area");   // write should fail
      FAILm("Write successful and should be not");
   } else {
      ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
      printf("Handled SIGSEGV on write-protected mem violation... continuing...\n");
   }

   // now unmap the leftovers. (ASSERT can call argument multiple times on failure, so using 'ret')
   ret = munmap(addr, 10 * MIB);
   ASSERT_EQ_FMTm("Unmap head", 0, ret, "%d");
   ret = munmap(addr1, 1 * MIB);
   ASSERT_EQ_FMTm("Unmap from the middle again", 0, ret, "%d");
   ret = munmap(addr1 + 1 * MIB, 200 * MIB - 10 * MIB - MIB);
   ASSERT_EQ_FMTm("Unmap tail", 0, ret, "%d");

   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
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

   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   CHECK_CALL(mmap_test(tests));
   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   PASS();
}

TEST mmap_file_test()
{
   static char fname[] = "/tmp/mmap_test_XXXXXX";
   if (greatest_get_verbosity() == 1) {
      printf("===== %s file template %s\n", __FUNCTION__, fname);
   }
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
   ASSERT_NOT_EQ(MAP_FAILED, m);

   // Check contents
   char* s = m;
   for (int i = 0; i < nbufs; i++) {
      ASSERT_EQ('a' + i, s[sizeof(buffer) * i]);
   }

   // Map contents of page[2] on page[1].
   void* t =
       mmap(s + sizeof(buffer), sizeof(buffer), PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, sizeof(buffer) * 2);
   ASSERT_NOT_EQ(MAP_FAILED, t);
   close(fd);
   ASSERT_EQ(s + sizeof(buffer), t);
   ASSERT_EQ('a' + 2, s[sizeof(buffer)]);
   int ret = munmap(m, nbufs * sizeof(buffer));
   ASSERT_EQ_FMT(0, ret, "%d");
   unlink(fname);
   PASS();
}

TEST mmap_file_test_ex(void* arg0)
{
   static const int bad_fd = 222;
   struct stat statbuf;
   int ret;
   void* t;

   int fd = open(arg0, O_RDONLY);
   fstat(fd, &statbuf);

   // should fail: bad FD (not convertable)
   t = mmap(0, statbuf.st_size, PROT_READ, MAP_PRIVATE, bad_fd, 0);
   ASSERT_EQ(MAP_FAILED, t);
   ASSERT_EQ_FMT(EBADF, errno, "%d");

   // should fail: MAP_FIXED with addr=0
   t = mmap(0, statbuf.st_size, PROT_READ, MAP_FIXED | MAP_PRIVATE, fd, 0);
   ASSERT_EQ(MAP_FAILED, t);
   ASSERT_EQ_FMT(EPERM, errno, "%d");

   // should fail on KM: size > GUEST_MEM_ZONE_SIZE_VA
   if (KM_PAYLOAD() == 1) {
      t = mmap(0, 600 * GIB, PROT_READ, MAP_PRIVATE, fd, 0);
      ASSERT_EQ(MAP_FAILED, t);
      ASSERT_EQ_FMT(ENOMEM, errno, "%d");
   }

   // success test  - check we open argv0 and it's really ELF
   t = mmap(0, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
   ASSERT_NOT_EQ(MAP_FAILED, t);
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);
   close(fd);

   char buf[KM_PAGE_SIZE];   // check we really have ELF there
   memcpy(buf, t, KM_PAGE_SIZE);
   ret = strncmp(buf + 1, "ELF", 3);
   ASSERT_EQ_FMT(0, ret, "%d");
   ret = munmap(t, statbuf.st_size);
   ASSERT_EQ_FMT(0, ret, "%d");

   // success test - ask for a file over pre-created mapping
   fd = open(arg0, O_RDONLY);
   fstat(fd, &statbuf);

   ASSERT_MMAPS_COUNT(2, TOTAL_MMAPS);
   t = mmap(0, statbuf.st_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   ASSERT_NOT_EQ(MAP_FAILED, t);

   // quick check that without MAP_FIXED the hint is ignored, so this should get a new map
   void* fmap = mmap(t, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
   ASSERT_NOT_EQ_FMT(t, fmap, "%p");
   ASSERT_MMAPS_COUNT(4, TOTAL_MMAPS);
   ret = munmap(fmap, statbuf.st_size);

   // And this should use the existing map
   fmap = mmap(t, statbuf.st_size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
   ASSERT_EQ_FMT(t, fmap, "%p");
   ASSERT_MMAPS_COUNT(3, TOTAL_MMAPS);

   close(fd);
   ret = munmap(t, statbuf.st_size);
   ASSERT_EQ_FMT(0, ret, "%d");
   PASS();
}

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
   RUN_TEST1(mmap_file_test_ex, argv[0]);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
