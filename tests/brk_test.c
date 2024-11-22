/*
 * Copyright 2021,2023 Kontain Inc
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
 * Test brk() with different values
 */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "syscall.h"

#define GIB (1ul << 30)
#define MIB (1ul << 20)
#define KIB (1ul << 10)

void* SYS_break(void const* addr)
{
   return (void*)syscall(SYS_brk, addr);
}

static inline char* simple_addr_reserve(size_t size)
{
   return mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static void const* __39_bit_mem = (void*)(512 * GIB);   // 512GB

static void const* high_addr = (void*)(768 * MIB);
static void const* very_high_addr = (void*)(512 * GIB);

/*
 * Test brk and tbrk co-existing in the same region.
 *
 * We test four regions, regs[] are the stating address. We put tbrk (via mmap) at the place defined
 * by map_off of that region. Than we try to put brk using the offs[] in that region, with the
 * intent of putting it well below, just 1GB below, same GB, and above. Each test iteration is mmap,
 * then break, then reset.
 *
 * tbrk/brk does mmap first brk second, brk/tbrk in the opposite order.
 *
 * Some of the 'offs' array values are supposed to succeed and  some are supposed to fail.
 * The 'offs_success' array keeps track of this.
 */

static unsigned long const regs[] = {64 * GIB, 128 * GIB, 256 * GIB, 384 * GIB};
static unsigned long const offs[] = {1 * GIB + MIB, 15 * GIB + MIB, 16 * GIB + 2 * MIB, 45 * GIB + MIB};
static int offs_success[] = {1, 1, 0, 0};
static unsigned long const map_off = 16 * GIB + 800 * MIB;

TEST tbrk_brk_test()
{
   void* brk = SYS_break(0);

   for (int r = 0; r < sizeof(regs) / sizeof(long); r++) {
      size_t map_s = (size_t)__39_bit_mem - regs[r] - map_off;
      for (int o = 0; o < sizeof(offs) / sizeof(long); o++) {
         char msg[128];

         sprintf(msg, "%s reg %d off %d 0x%lx", "mmap", r, o, map_s);
         void* map_p = simple_addr_reserve(map_s);
         ASSERT_NEQ_FMTm(strdup(msg), map_p, MAP_FAILED, "%p");

         void* brk_exp = (void*)(regs[r] + offs[o]);
         sprintf(msg, "%s reg %d off %d %p", "brk ", r, o, brk_exp);
         void* brk_got = SYS_break(brk_exp);
         if (!offs_success[o]) {
            brk_exp = (void*)-1;
         }
         ASSERT_EQ_FMTm(strdup(msg), brk_exp, brk_got, "%p");

         munmap(map_p, map_s);
         brk_got = SYS_break(brk);
         ASSERT_EQ_FMTm("brk", brk, brk_got, "%p");
      }
   }
   PASS();
}

TEST brk_tbrk_test()
{
   void* brk = SYS_break(0);

   for (int r = 0; r < sizeof(regs) / sizeof(long); r++) {
      size_t map_s = (size_t)__39_bit_mem - regs[r] - map_off;
      for (int o = 0; o < sizeof(offs) / sizeof(long); o++) {
         char msg[128];

         void* brk_exp = (void*)(regs[r] + offs[o]);
         sprintf(msg, "%s reg %d off %d %p", "brk ", r, o, brk_exp);
         void* brk_got = SYS_break(brk_exp);
         ASSERT_EQ_FMTm(strdup(msg), brk_exp, brk_got, "%p");

         sprintf(msg, "%s reg %d off %d", "mmap", r, o);
         void* map_p = simple_addr_reserve(map_s);
         if (offs_success[o]) {
            ASSERT_NEQ_FMTm(strdup(msg), map_p, MAP_FAILED, "%p");
         } else {
            ASSERT_EQ_FMTm(strdup(msg), map_p, MAP_FAILED, "%p");
         }

         munmap(map_p, map_s);
         brk_got = SYS_break(brk);
         ASSERT_EQ_FMTm("brk", brk, brk_got, "%p");
      }
   }
   PASS();
}

TEST brk_2gib(void)
{
   // Check reaching into 2nd GB (1gb pages).
   void* _1gb = (void*)GIB + 2 * MIB;
   SYS_break(_1gb);
   ASSERT_EQ_FMT(_1gb, SYS_break(NULL), "%p");
   PASS();
}

TEST brk_test(void)
{
   void *ptr, *ptr1;

   printf("break is %p\n", ptr = SYS_break(NULL));
   SYS_break(high_addr);
   printf("break is %p, expected %p\n", ptr1 = SYS_break(NULL), high_addr);
   ASSERT_EQ(ptr1, high_addr);

   ptr1 -= 20;
   strcpy(ptr1, "Hello, world");
   printf("%s from far up the memory %p\n", (char*)ptr1, ptr1);

   if (SYS_break(very_high_addr) != very_high_addr) {
      perror("Unable to set brk that high (expected)");
      ASSERT(very_high_addr >= __39_bit_mem);
   } else {
      printf("break is %p\n", ptr1 = SYS_break(NULL));

      ptr1 -= 20;
      strcpy(ptr1, "Hello, world");
      printf("%s from even farer up the memory %p\n", (char*)ptr1, ptr1);

      ASSERT(very_high_addr < __39_bit_mem);
   }

   SYS_break((void*)ptr);
   ASSERT_EQ(ptr, SYS_break(NULL));
   PASS();
}

TEST brk_not_too_greedy()
{
   static void* brk_up = (void*)(GIB * 200);
   void* brk = SYS_break(NULL);

   ASSERT_NEQ(SYS_break(NULL), (void*)-1);
   ASSERT_EQ_FMT(SYS_break(brk_up), brk_up, "%p");
   ASSERT_EQ_FMT(SYS_break(NULL), brk_up, "%p");
   SYS_break(brk);

   PASS();
}

jmp_buf jbuf;
void access_fault(int signal)
{
   siglongjmp(jbuf, 1);
}

/*
 * Set the break to brkaddr then verify that we can write
 * below the break and can't write above the break.
 * Returns:
 *   0 - success
 *   1 - memory access failed
 *   2 - brk() call failed
 * We expect the brkaddr arg to be on a page boundary.
 */
int setbrk_checkaccess(void* brkaddr)
{
   int sjrc;
   char* pokehere;

   if (SYS_break(brkaddr) != brkaddr) {
      printf("Set break to %p failed\n", brkaddr);
      return 2;
   }
   pokehere = brkaddr;
   if ((sjrc = sigsetjmp(jbuf, 1)) == 0) {
      *(pokehere - 1) = 99;
   }
   if (sjrc != 0) {
      // Can't write below the break
      printf("Can't write below the break at %p\n", pokehere - 1);
      return 1;
   }
   if ((sjrc = sigsetjmp(jbuf, 1)) == 0) {
      *(pokehere + 1) = 88;
   }
   if (sjrc != 1) {
      // We can write above the break, really?
      printf("Can write above the break at %p\n", pokehere + 1);
      return 1;
   }
   return 0;
}

/*
 * Test brk() which shrinks the brk region of memory.
 * We try things like brk() completely within in the 1g page region,
 * brk() completely within the 2m page region and brk from the 1g
 * page region into the 2m page region.
 * We verify that memory below the new brk is still accessible and memory above
 * the new break is not accessible.
 * And, we also test brk() to pdpe and pde boundaries and brk() to
 * non-pdpe and non-pde boundaries and verify that the memory we expect to be
 * accessible is accessible.
 */
TEST brk_shrink()
{
   void* ptr;
   struct sigaction sa;
   void* brkaddr;

   sa.sa_handler = access_fault;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   ASSERT_EQ(sigaction(SIGSEGV, &sa, NULL), 0);

   printf("initial break is %p\n", ptr = SYS_break(NULL));
   ptr = (void*)(((unsigned long)ptr + 4095) & ~4095);

   // Set brk in the 1g page region of the page table
   brkaddr = (void*)(8 * GIB);
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop down to the next lower 1g boundary.
   brkaddr -= 1 * GIB;
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop down to inside the 1g page
   brkaddr -= 128 * MIB;
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop down to 1g
   brkaddr = (void*)(1 * GIB);
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop down into the 2m page region of the page table
   brkaddr = (void*)(512 * MIB);
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop to an address not on a 2m boundary
   brkaddr -= 1 * MIB;
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Move back into the 1g page region of the page table
   brkaddr = (void*)(1 * GIB + 16 * MIB);
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Drop down into the 2m page region of the page table
   brkaddr = (void*)(1 * GIB - 128 * KIB);
   ASSERT_EQ(setbrk_checkaccess(brkaddr), 0);

   // Put the break back to where it was
   SYS_break((void*)ptr);
   ASSERT_EQ(ptr, SYS_break(NULL));

   // Turn off signal handler
   sa.sa_handler = SIG_DFL;
   ASSERT_EQ(sigaction(SIGSEGV, &sa, NULL), 0);

   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(brk_shrink);

   RUN_TEST(brk_2gib);
   RUN_TEST(brk_test);
   RUN_TEST(brk_not_too_greedy);
   RUN_TEST(brk_test);
   RUN_TEST(brk_not_too_greedy);

   RUN_TEST(tbrk_brk_test);
   RUN_TEST(brk_tbrk_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
