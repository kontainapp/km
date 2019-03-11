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
 * Test brk() with different values
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

void* SYS_break(void const* addr)
{
   return (void*)syscall(SYS_brk, addr);
}

static inline char* simple_mmap(size_t size)
{
   return mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

static void const* __39_bit_mem = (void*)(512 * GIB);   // 512GB

static void const* high_addr = (void*)(768 * MIB);
static void const* very_high_addr = (void*)(512 * GIB);

/*
 * Test brk and tbrk co-existing in the same region.
 * 
 * We test four regions, regs[] are the stating address. We put tbrk (via mmap) at the place defined
 * by map_off of that region. Than we try to put brk using the offs[] in that region, with the
 * intent of putting it well below, just 1GB below, same GB, and above. Each test iteration is mmap, then break, then reset.
 * 
 * tbrk/brk does mmap first brk second, brk/tbrk in the opposite order.
 */

static unsigned long const regs[] = {64 * GIB, 128 * GIB, 256 * GIB, 384 * GIB};
static unsigned long const offs[] = {1 * GIB + MIB, 15 * GIB + MIB, 16 * GIB + 2 * MIB, 45 * GIB + MIB};
static unsigned long const map_off = 16 * GIB + 800 * MIB;

TEST tbrk_brk_test()
{
   SKIPm("TODO: Implement tbrk and brk in the same memory region, km_mem_brk() and km_mem_tbrk");
   void* brk = SYS_break(0);

   for (int r = 0; r < sizeof(regs) / sizeof(long); r++) {
      size_t map_s = (size_t)__39_bit_mem - regs[r] - map_off;
      for (int o = 0; o < sizeof(offs) / sizeof(long); o++) {
         char msg[128];

         sprintf(msg, "%s reg %d off %d 0x%lx", "mmap", r, o, map_s);
         void* map_p = simple_mmap(map_s);
         ASSERT_NOT_EQ_FMTm(msg, map_p, MAP_FAILED, "%p");

         void* brk_exp = (void*)(regs[r] + offs[o]);
         sprintf(msg, "%s reg %d off %d %p", "brk ", r, o, brk_exp);
         void* brk_got = SYS_break(brk_exp);
         ASSERT_EQ_FMTm(msg, brk_exp, brk_got, "%p");

         munmap(map_p, map_s);
         brk_got = SYS_break(brk);
         ASSERT_EQ_FMTm("brk", brk, brk_got, "%p");
      }
   }
   PASS();
}

TEST brk_tbrk_test()
{
   SKIPm("TODO: Implement tbrk and brk in the same memory region, km_mem_brk() and km_mem_tbrk");
   void* brk = SYS_break(0);

   for (int r = 0; r < sizeof(regs) / sizeof(long); r++) {
      size_t map_s = (size_t)__39_bit_mem - regs[r] - map_off;
      for (int o = 0; o < sizeof(offs) / sizeof(long); o++) {
         char msg[128];

         void* brk_exp = (void*)(regs[r] + offs[o]);
         sprintf(msg, "%s reg %d off %d %p", "brk ", r, o, brk_exp);
         void* brk_got = SYS_break(brk_exp);
         ASSERT_EQ_FMTm(msg, brk_exp, brk_got, "%p");

         sprintf(msg, "%s reg %d off %d", "mmap", r, o);
         void* map_p = simple_mmap(map_s);
         ASSERT_NOT_EQ_FMTm(msg, map_p, MAP_FAILED, "%p");

         munmap(map_p, map_s);
         brk_got = SYS_break(brk);
         ASSERT_EQ_FMTm("brk", brk, brk_got, "%p");
      }
   }
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
   void* brk = sbrk(0);

   ASSERT_NOT_EQ(SYS_break(NULL), (void*)-1);
   ASSERT_EQ_FMT(SYS_break(brk_up), brk_up, "%p");
   ASSERT_EQ(sbrk(0), brk_up);
   SYS_break(brk);

   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(brk_test);
   RUN_TEST(brk_not_too_greedy);
   RUN_TEST(brk_test);
   RUN_TEST(brk_not_too_greedy);

   RUN_TEST(tbrk_brk_test);
   RUN_TEST(brk_tbrk_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}