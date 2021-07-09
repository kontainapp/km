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
