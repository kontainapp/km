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

#include "mmap_test.h"

TEST simple_test()
{
   static mmap_test_t madvice_tests[] =
       {{__LINE__, "1. Large Empty mmap", TYPE_MMAP, 0, 64 * MIB, PROT_WRITE, flags, OK},
        {__LINE__, "2a. OK to WRITE", TYPE_WRITE, 5 * MIB, 20 * MIB, '2', 0, OK},
        {__LINE__,
         "3. madvise MADV_DONTNEED should fail",
         TYPE_MADVISE,
         100 * MIB,
         10 * MIB,
         0,
         flags,
         ENOMEM,
         MADV_DONTNEED},
        {__LINE__, "3a. madvise MADV_FREE, EINVAL fail", TYPE_MADVISE, 8 * MIB, 10 * MIB, 0, flags, EINVAL, MADV_FREE},
        {__LINE__, "3b. OK to READ, should read in not 0", TYPE_READ, 9 * MIB, 1 * MIB, '2', 0, OK, 0},
        {__LINE__, "3c. madvise MADV_DONTNEED", TYPE_MADVISE, 8 * MIB, 10 * MIB, 0, flags, OK, MADV_DONTNEED},
        {__LINE__, "4.  OK to READ", TYPE_READ, 9 * MIB, 1 * MIB, 0, 0, OK, 0},
        {0}};
   if (greatest_get_verbosity() != 0) {
      printf("Running %s\n", __FUNCTION__);
   }
   CHECK_CALL(mmap_test(madvice_tests));
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
