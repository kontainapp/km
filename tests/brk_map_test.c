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

/*
 * Test brk() with configrable memory size.
 *
 * Note: Two sets of SYS_break tests is temporary until I work out how to handle setable
 *       guest virtual memory and tests in general.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "syscall.h"

#define GIB (1ul << 30)
#define MIB (1ul << 20)

unsigned long max_pmem = 0;

void* SYS_break(void const* addr)
{
   return (void*)syscall(SYS_brk, addr);
}

static inline char* simple_mmap(size_t size)
{
   return mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

#define TEST_BUFFER_SZ 1024
char buf[TEST_BUFFER_SZ];

/*
 * Test that data written to mmap or SYS_break memory can be written and
 * read back. Use random'ish data for each call to ensure we don't succeed
 * due to old data in memory.
 *
 * Returns 1 (True) when successful, 0 (False) otherwise.
 */
int test_memory(void* ptr)
{
   for (int i = 0; i < TEST_BUFFER_SZ; i++) {
      buf[i] = (char)(rand() % 256);
   }
   memcpy(ptr, buf, TEST_BUFFER_SZ);
   return (memcmp(ptr, buf, TEST_BUFFER_SZ) == 0);
}

// SYS_break to 2 GIB less than all memory. Shoud succeed.
// Note: This does pretty much the same thing as:
//  brk_test.c:brk_test()
//  brk_test.c:brk_not_to_greedy()
TEST brk_almost_all(void)
{
   // break to 2GIB less than phys memory.
   void* prev = SYS_break(NULL);
   void* almost_all = (void*)(max_pmem - 2 * GIB);
   void* result;
   result = SYS_break((void*)(max_pmem - 2 * GIB));
   ASSERT_EQ_FMT(almost_all, result, "%p");
   ASSERT_EQ_FMT(almost_all, SYS_break(NULL), "%p");

   // Mess with .
   char* p = (char*)prev;
   int rc = test_memory(p);
   ASSERT_NEQ_FMT(0, rc, "%d");

   p = ((char*)result) - TEST_BUFFER_SZ;
   rc = test_memory(p);
   ASSERT_NEQ_FMT(0, rc, "%d");

   // restore previous state.
   result = SYS_break(prev);
   ASSERT_EQ_FMT(prev, result, "%p");
   PASS();
}

// SYS_break to 1 GIB less than all physical memory. Should fail.
TEST brk_too_much(void)
{
   // break to 1GIB less than phys memory. Should fail.
   void* prev = SYS_break(NULL);
   void* almost_all = (void*)(max_pmem - GIB);
   void* result;
   result = SYS_break(almost_all);
   ASSERT_EQ_FMT((void*)-1, result, "%p");
   ASSERT_EQ_FMT(prev, SYS_break(NULL), "%p");
   PASS();
}

// mmap 2 GIB less than all physical memory. Should succeed.
TEST map_almost_all(void)
{
   // mmap to 2GIB less than phys memory.
   size_t mapsz = max_pmem - 2 * GIB;
   void* ptr = simple_mmap(mapsz);
   ASSERT_NEQ_FMT(MAP_FAILED, ptr, "%p");
   // See if we can modify
   char* p = (char*)ptr;
   int rc = test_memory(p);
   ASSERT_NEQ_FMT(0, rc, "%d");

   p = ((char*)ptr) + 2 * GIB - TEST_BUFFER_SZ;
   rc = test_memory(p);
   ASSERT_NEQ_FMT(0, rc, "%d");

   munmap(ptr, mapsz);
   PASS();
}

// mmap  1 GIB less than all phyical memory. Should fail.
TEST map_too_much(void)
{
   // mmap to 1 GIB less than phys memory. Should fail.
   void* ptr = simple_mmap(max_pmem - GIB);
   ASSERT_EQ_FMT(MAP_FAILED, ptr, "%p");
   PASS();
}

// 1. mmap 1/2 physical memory.
// 2. SYS_break to 1GIB less than mmap base.
// Should work.
TEST map_brk_to_limit(void)
{
   size_t mapsz = max_pmem / 2;
   void* ptr = simple_mmap(mapsz);
   ASSERT_NEQ_FMT(MAP_FAILED, ptr, "%p");

   void* prev = SYS_break(NULL);
   // Note: assming va == pa in 'brk' region.
   size_t brklim = ((size_t)ptr) & (max_pmem - 1);
   void* result = SYS_break((void*)(brklim - GIB));
   ASSERT_NEQ_FMT((void*)-1, result, "%p");

   int rc;
   // Test memory in map
   rc = test_memory(ptr);
   ASSERT_NEQ_FMT(0, rc, "%d");

   // Test memory in brk area (1st and last buffer)
   void* curbrk = SYS_break(NULL);
   rc = test_memory(prev);
   ASSERT_NEQ_FMT(0, rc, "%d");
   rc = test_memory((char*)curbrk - TEST_BUFFER_SZ);
   ASSERT_NEQ_FMT(0, rc, "%d");

   // put everything back the way it was.
   SYS_break(prev);
   munmap(ptr, mapsz);

   PASS();
}

// 1. SYS_break to 1GIB less than mmap base.
// 2. mmap 1/2 physical memory.
// Should work.
TEST brk_map_to_limit(void)
{
   // cheat and see where a 1/2 phys mem map would fall.
   size_t mapsz = max_pmem / 2;
   void* ptr = simple_mmap(mapsz);
   munmap(ptr, mapsz);

   void* prev = SYS_break(NULL);
   // Note: assming va == pa in 'brk' region.
   size_t brklim = ((size_t)ptr) & (max_pmem - 1);
   void* result = SYS_break((void*)(brklim - GIB));
   ASSERT_NEQ_FMT((void*)-1, result, "%p");

   // really mmap now.
   ptr = simple_mmap(mapsz);
   ASSERT_NEQ_FMT(MAP_FAILED, ptr, "%p");

   int rc;
   // Test memory in map
   rc = test_memory(ptr);
   ASSERT_NEQ_FMT(0, rc, "%d");

   // Test memory in brk area (1st and last buffer)
   void* curbrk = SYS_break(NULL);
   rc = test_memory(prev);
   ASSERT_NEQ_FMT(0, rc, "%d");
   rc = test_memory((char*)curbrk - TEST_BUFFER_SZ);
   ASSERT_NEQ_FMT(0, rc, "%d");

   // put everything back the way it was.
   SYS_break(prev);
   munmap(ptr, mapsz);

   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   extern int optind;
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   if (optind + 2 != argc) {
      fprintf(stderr, "usage: %s [GREATEST options] -- <bus size (bits)>\n", argv[0]);
      exit(1);
   }
   char* ep = NULL;
   int pbus_size = strtol(argv[optind + 1], &ep, 0);
   if (ep == NULL || *ep != '\0') {
      fprintf(stderr, "'%s' is not a number\n", argv[optind + 1]);
      exit(1);
   }
   max_pmem = 1ULL << pbus_size;
   if (max_pmem == 0) {
      fprintf(stderr, "invalid bus size - too large");
      exit(1);
   }

   RUN_TEST(brk_almost_all);
   RUN_TEST(brk_too_much);
   RUN_TEST(map_almost_all);
   RUN_TEST(map_too_much);

   RUN_TEST(map_brk_to_limit);
   RUN_TEST(brk_map_to_limit);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
