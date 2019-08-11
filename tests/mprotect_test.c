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
 * Test reaching out above brk to see if mprotect catches it
 * Also tests that mprotect/munmap/mmap combination sets proper mprotect-ed areas
 *
 * Note that mmap_test.c is the test coverign mmap() protection flag test, as well as basic
 * protection for munmapped areas, so here we mainly focus on brk() and mprotect() call
 */
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include "syscall.h"

#include "greatest/greatest.h"
#include "mmap_test.h"

// TODO - check with Serge what this padding was for
// static char pad[0x400000] = " padding";
// void inline IGUR()
// {
// } /* Ignore GCC Unused Result */

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

// check that write() syscall is propertly handing wrong address, and that reaching above brk() fails
TEST brk_test()
{
   char* ptr_above_brk = (char*)roundup((unsigned long)sbrk(0), 0x1000);
   int ret;

   if (write(1, ptr_above_brk, 1024) >= 0 || errno != EFAULT) {
      FAILm("Write above brk() should fail with EFAULT");
   }

   signal(SIGSEGV, signal_handler);
   if ((ret = setjmp(jbuf)) == 0) {
      strcpy((char*)ptr_above_brk, "writing above brk area");
      FAILm("Write successful and should be not");   // return
   }

   signal(SIGSEGV, SIG_DFL);
   ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
   if (fail == 1) {
      FAILm("signal handler reported a failure");
   }
   PASSm("Handled SIGSEGV above brk() successfully...\n");
}

// traverse the tests table and execute the tests
TEST mmap_test_execute(mmap_test_t* tests)
{
   int ret;
   void* last_addr = MAP_FAILED;   // changed by mmap

   for (mmap_test_t* t = tests; t->test_info != NULL; t++) {
      static const char* errno_fmt = "errno 0x%x";   // format for offsets/types error msg
      static const char* ret_fmt = "ret 0x%x";       // format for offsets/types error msg

      errno = 0;
      fail = 0;
      printf("%s: op %d (%s, %s...) \n", t->test_info, t->type, out_sz(t->offset), out_sz(t->size));

      switch (t->type) {
         case TYPE_MMAP:
            last_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0);
            printf("return: %p (%s)\n", last_addr, out_sz((uint64_t)last_addr));

            if (t->expected_failure == OK) {
               ASSERT_EQ_FMTm(t->test_info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->test_info, MAP_FAILED, last_addr, ret_fmt);
               if ((t->prot & PROT_WRITE) != 0) {
                  printf("Map OK, trying to memset '2' to 0x%lx size: 0x%lx\n",
                         (uint64_t)last_addr,
                         t->size);
                  memset(last_addr, '2', t->size);
               }
            } else {
               ASSERT_EQ_FMTm(t->test_info, MAP_FAILED, last_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->test_info, t->expected_failure, errno, errno_fmt);
            }
            break;
         case TYPE_MUNMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = munmap(last_addr + t->offset, t->size);
            if (t->expected_failure == OK) {
               ASSERT_EQ_FMTm(t->test_info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->test_info, t->expected_failure, errno, errno_fmt);
            }
            break;
         case TYPE_MPROTECT:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = mprotect(last_addr + t->offset, t->size, t->prot);
            if (t->expected_failure == OK) {
               ASSERT_EQ_FMTm(t->test_info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->test_info, t->expected_failure, errno, errno_fmt);
            }
            break;
         case TYPE_WRITE:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected_failure == OK) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               break;
            }
            signal(t->expected_failure, signal_handler);
            if ((ret = setjmp(jbuf)) == 0) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               FAILm("Write successful and should be not");   // return
            }
            assert(ret == SIGSEGV);   // we use that value in longjmp
            signal(t->expected_failure, SIG_DFL);
            ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            break;
         case TYPE_READ:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected_failure == OK) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }
               break;
               signal(t->expected_failure, signal_handler);
               if ((ret = setjmp(jbuf)) == 0) {
                  for (size_t i = 0; i < t->size; i++) {
                     volatile char c = *(char*)(last_addr + t->offset + i);
                     assert(c != c + 1);   // stop gcc from complaining,but generate code
                  }
                  FAILm("Read successful and should be not");   // return
               }
               assert(ret == SIGSEGV);   // we use that value in longjmp
               signal(t->expected_failure, SIG_DFL);
               ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            }
            break;
         default:
            ASSERT_EQ(NULL, "Not reachable");
      }
   }
   PASS();
}

// just to type less going forward
static const int flags = (MAP_SHARED | MAP_ANONYMOUS);

// get mmap(PROT_NONE), cut sections with different protections and check read/write there
TEST simple_test()   // TODO
{
   static mmap_test_t mprotect_tests[] = {
       {"1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {"2. mprotect PROT_READ", TYPE_MPROTECT, 8 * MIB, 10 * MIB, PROT_READ, flags, OK},
       {"2a.should fail to write", TYPE_WRITE, 8 * MIB, 1 * MIB, '2', 0, SIGSEGV},
       {"2a.OK to read", TYPE_READ, 9 * MIB, 1 * MIB, 0, 0, OK},
       {"3. mprotect PROT_WRITE", TYPE_MPROTECT, 20 * MIB, 10 * MIB, PROT_WRITE, flags, OK},
       {"3a.OK to write", TYPE_WRITE, 21 * MIB, 1 * MIB, '3', 0, OK},
       {"3a.should fail to read", TYPE_READ, 23 * MIB, 2 * MIB, 0, 0, SIGSEGV},
       {"4. mprotect large READ|WRITE", TYPE_MPROTECT, 6 * MIB, 32 * MIB, PROT_READ | PROT_WRITE, flags, OK},
       {"4a.OK to write", TYPE_WRITE, 8 * MIB, 1 * MIB, '4', 0, OK},
       {"4a.OK to read", TYPE_READ, 16 * MIB, 2 * MIB, 0, 0, OK},
       {"5.cleanup unmap", TYPE_MUNMAP, 0, 64 * MIB, PROT_NONE, flags, OK},

       // test mprotect merge
       {"6. Large Empty mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {"6a. mprotect PROT_READ", TYPE_MPROTECT, 1 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"6b. mprotect PROT_READ", TYPE_MPROTECT, 0 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"7.cleanup unmap", TYPE_MUNMAP, 0, 2 * MIB, PROT_NONE, flags, OK},

       {NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test_execute(mprotect_tests));
   PASS();
}

// test tricky combinations of mmap/munmap/mprotect zones.
TEST complex_test()
{
   static mmap_test_t mprotect_tests[] = {
       {"1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {"1a.Poke a hole - unmap", TYPE_MUNMAP, 1 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {"1b.Should fail - inside", TYPE_MPROTECT, 1 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {"1c.Should fail  - aligned", TYPE_MPROTECT, 1 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {"1d.Unmap more from 0", TYPE_MUNMAP, 0, 1 * MIB, PROT_NONE, flags, OK},
       {"1e. Seal the map Empty mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {"2. Large mprotect", TYPE_MPROTECT, 8 * MIB, 22 * MIB, PROT_READ | PROT_WRITE, flags, OK},
       {"3. Hole1 mprotect", TYPE_MPROTECT, 16 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {"4. Hole2 mprotect", TYPE_MPROTECT, 20 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {"5. Glue holes mprotect", TYPE_MPROTECT, 1 * MIB, 63 * MIB, PROT_READ | PROT_WRITE | PROT_EXEC, flags, OK},
       {"6. Hole unmap", TYPE_MUNMAP, 12 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {"6a.Should fail on gap1", TYPE_MPROTECT, 10 * MIB, 10 * MIB, PROT_NONE, flags, ENOMEM},
       {"6b.Should fail of gap2", TYPE_MPROTECT, 12 * MIB, 3 * MIB, PROT_NONE, flags, ENOMEM},
       {"7. Hole3 mprotect  ", TYPE_MPROTECT, 10 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"8. Unmap over unmap", TYPE_MUNMAP, 10 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {"9. Should fail on gaps", TYPE_MPROTECT, 8 * MIB, 16 * MIB, PROT_READ, flags, ENOMEM},
       {"10.Gaps1", TYPE_MPROTECT, 0, 2 * GIB, PROT_NONE, flags, ENOMEM},
       {"11.Gaps2", TYPE_MPROTECT, 10 * MIB, 2 * MIB, PROT_NONE, flags, ENOMEM},
       {"12.fill in mmap", TYPE_MMAP, 0, 16 * MIB, PROT_NONE, flags, OK},   // should fill in the hole
       {"13.fill in mprotect", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {"14.cleanup unmap", TYPE_MUNMAP, 0, 64 * MIB, PROT_NONE, flags, OK},

       {NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test_execute(mprotect_tests));
   PASS();
}

// helper to test glue
TEST concat_test()
{
   static mmap_test_t mprotect_tests[] = {
       {"1. mmap", TYPE_MMAP, 0, 1 * MIB, PROT_NONE, flags, OK},
       {"2. mmap", TYPE_MMAP, 0, 2 * MIB, PROT_NONE, flags, OK},
       {"3. mmap", TYPE_MMAP, 0, 3 * MIB, PROT_NONE, flags, OK},
       {"4. mmap", TYPE_MMAP, 0, 4 * MIB, PROT_NONE, flags, OK},
       {"5. mmap", TYPE_MMAP, 0, 5 * MIB, PROT_NONE, flags, OK},
       {"6. mprotect", TYPE_MPROTECT, 1 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"7. mprotect", TYPE_MPROTECT, 2 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"8. mprotect", TYPE_MPROTECT, 3 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"9. mprotect", TYPE_MPROTECT, 4 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"10.mprotect", TYPE_MPROTECT, 5 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"11.mprotect", TYPE_MPROTECT, 6 * MIB, 1 * MIB, PROT_READ, flags, OK},

       // TODO: automate checking for the mmaps concatenation. For now check with `print_tailq
       // &machine.mmaps.busy ` in gdb
       {"12.cleanup unmap", TYPE_MUNMAP, 0, 15 * MIB, PROT_NONE, flags, OK},

       {NULL},
   };

   printf("Running %s\n", __FUNCTION__);
   CHECK_CALL(mmap_test_execute(mprotect_tests));
   PASS();
}
GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(brk_test);
   RUN_TEST(simple_test);
   RUN_TEST(concat_test);
   RUN_TEST(complex_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
