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

/*
 * A call to this function allows to breakpoint in gdb on dummy_hcall().
 * If getppid will be mapped to something else, this function need to be updated as well  */
static void km_break(void)
{
   (void)syscall(SYS_getppid, 0);
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
TEST mprotect_brk_test()
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

// test tricky combinations of mmap/munmap/mprotect zones.
TEST mprotect_complex_test()
{
   int ret;
   static const int flags = (MAP_SHARED | MAP_ANONYMOUS);

   static mmap_test_t mprotect_tests[] = {
       {"1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {"2. Large mprotect", TYPE_MPROTECT, 8 * MIB, 22 * MIB, PROT_READ | PROT_WRITE, flags, OK},
       {"3. Hole1 mprotect", TYPE_MPROTECT, 16 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {"4. Hole2 mprotect", TYPE_MPROTECT, 20 * MIB, 1 * MIB, PROT_NONE, flags, OK},
       {"5. Glue holes mprotect", TYPE_MPROTECT, 10 * MIB, 15 * MIB, PROT_READ | PROT_WRITE, flags, OK},
       {"6. Hole unmap", TYPE_MUNMAP, 12 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {"7. Hole3 mprotect", TYPE_MPROTECT, 10 * MIB, 1 * MIB, PROT_READ, flags, OK},
       {"8. Unmap over unmap", TYPE_MUNMAP, 10 * MIB, 14 * MIB, PROT_NONE, flags, OK},
       {"9. Fail mrotect - holes", TYPE_MPROTECT, 8 * MIB, 16 * MIB, PROT_READ, flags, ENOMEM},
       {"10.Fail1 mprotect", TYPE_MPROTECT, 0, 2 * GIB, PROT_NONE, flags, ENOMEM},
       {"11.Fail2 mprotect", TYPE_MPROTECT, 10 * MIB, 2 * MIB, PROT_NONE, flags, ENOMEM},
       {"12.fill in mmap", TYPE_MMAP, 0, 16 * MIB, PROT_NONE, flags, OK},   // should fill in the hole
       {"13.fill in mprotect", TYPE_MMAP, 0, 64 * MIB, PROT_NONE, flags, OK},
       {"14.cleanup unmap", TYPE_MUNMAP, 0, 64 * MIB, PROT_NONE, flags, OK},

       {NULL},
   };
   static const char* errno_fmt = "errno 0x%x";   // format for offsets/types error msg
   static const char* ret_fmt = "ret 0x%x";       // format for offsets/types error msg

   void* last_addr = MAP_FAILED;   // changed by mmap; MAP_FAILED if mmap fails

   for (mmap_test_t* t = mprotect_tests; t->test_info != NULL; t++) {
      errno = 0;
      switch (t->type) {
         case TYPE_MMAP:
            printf("%s: mmap(%s, %s...) ", t->test_info, out_sz(t->offset), out_sz(t->size));
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
            printf("%s: mumap(%s, %s...)\n", t->test_info, out_sz(t->offset), out_sz(t->size));
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
            printf("%s: protect(%s, %s...)\n", t->test_info, out_sz(t->offset), out_sz(t->size));
            ret = mprotect(last_addr + t->offset, t->size, t->prot);
            if (t->expected_failure == OK) {
               ASSERT_EQ_FMTm(t->test_info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->test_info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->test_info, t->expected_failure, errno, errno_fmt);
            }
            break;
         default:
            ASSERT_EQ(NULL, "Not reachable");
      }
      km_break();   // allows to set GDB breakpoint on KM:dummy_hcall()
   }
   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(mprotect_brk_test);
   km_break();

   RUN_TEST(mprotect_complex_test);
   km_break();

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
