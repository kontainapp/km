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
 * protection for munmapped areas, so here we mainly focus on madvise call
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
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

// SIGSEGV handler
static sigjmp_buf jbuf;
static int fail = 0;
void signal_handler(int signal)
{
   if (signal != SIGSEGV) {
      warn("Unexpected signal caught: %d", signal);
      fail = 1;
   }
   siglongjmp(jbuf, SIGSEGV);
}

TEST madvice_execute(mmap_test_t* tests)
{
   int ret;
   void* last_addr = MAP_FAILED;
   for (mmap_test_t* t = tests; t->info != NULL; t++) {
      errno = 0;
      fail = 0;
      if (greatest_get_verbosity() > 0) {
         printf("%s: op %d (%s, %s...) \n", t->info, t->type, out_sz(t->offset), out_sz(t->size));
      }

      switch (t->type) {
         case TYPE_MMAP:
            last_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0);
            if (greatest_get_verbosity() != 0) {
               printf("return: %p (%s)\n", last_addr, out_sz((uint64_t)last_addr));
            }
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->info, MAP_FAILED, last_addr, ret_fmt);
               if ((t->prot & PROT_WRITE) != 0) {
                  if (greatest_get_verbosity() != 0) {
                     printf("Map OK, trying to memset '2' to 0x%lx size: 0x%lx\n",
                            (uint64_t)last_addr,
                            t->size);
                  }
                  memset(last_addr, '2', t->size);
               }
            } else {
               ASSERT_EQ_FMTm(t->info, MAP_FAILED, last_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_MUNMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = munmap(last_addr + t->offset, t->size);
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_WRITE:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               break;
            }
            signal(t->expected, signal_handler);
            if ((ret = sigsetjmp(jbuf, 1)) == 0) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               FAILm("Write successful and should be not");   // return
            }
            assert(ret == SIGSEGV);   // we use that value in longjmp
            signal(t->expected, SIG_DFL);
            ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            break;
         case TYPE_READ:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  if (t->prot == 0) {   // if t->prot 0 expect 0's, else expect c to equal
                                        // t->prot to verify proper read in
                     ASSERT_EQm("read in not 0's even though it should be", c, t->prot);
                  } else {
                     ASSERT_EQm("t->prot should equal c", c, (char)t->prot);
                  }

                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }

               break;
            }
            signal(t->expected, signal_handler);
            if ((ret = sigsetjmp(jbuf, 1)) == 0) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }
               FAILm("Read successful and should be not");   // return
               assert(ret == SIGSEGV);                       // we use that value in longjmp
               signal(t->expected, SIG_DFL);
               ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            }
            break;
         case TYPE_MADVISE:
            assert(last_addr != MAP_FAILED);

            ret = madvise(last_addr + t->offset, t->size, t->advise);
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
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
            break;
         default:
            ASSERT_EQ(NULL, "Not reachable");
      }
   }
   PASS();
}

TEST simple_test()
{
   static mmap_test_t madvice_tests[] = {
       {__LINE__, "1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_WRITE, flags, OK},
       {__LINE__, "2a. OK to WRITE", TYPE_WRITE, 5 * MIB, 20 * MIB, '2', 0, OK},
       {__LINE__, "3. madvise MADV_DONTNEED should fail", TYPE_MADVISE, 100 * MIB, 10 * MIB, 0, flags, ENOMEM, MADV_DONTNEED},
       {__LINE__, "3a. madvise MADV_FREE, EINVAL fail", TYPE_MADVISE, 8 * MIB, 10 * MIB, 0, flags, EINVAL, MADV_FREE},
       {__LINE__, "3b. OK to READ, should read in not 0", TYPE_READ, 9 * MIB, 1 * MIB, '2', 0, OK, 0},
       {__LINE__, "3c. madvise MADV_DONTNEED", TYPE_MADVISE, 8 * MIB, 10 * MIB, 0, flags, OK, MADV_DONTNEED},
       {__LINE__, "4.  OK to READ", TYPE_READ, 9 * MIB, 1 * MIB, 0, 0, OK, 0},
       {__LINE__, "5. OK to WRITE", TYPE_WRITE, 9 * MIB, 1 * MIB, '2', 0, OK},
   };
   if (greatest_get_verbosity() != 0) {
      printf("Running %s\n", __FUNCTION__);
   }
   CHECK_CALL(madvice_execute(madvice_tests));
   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(simple_test);
   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
