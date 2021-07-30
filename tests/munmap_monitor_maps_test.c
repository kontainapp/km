/*
 * Copyright 2021 Kontain Inc
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
 * munmap monitor private mappings
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "greatest/greatest.h"
#include "mmap_test.h"

TEST munmap_monitor_maps_test(void)
{
   void* gdtaddr = (void*)GUEST_MEM_TOP_VA - KM_PAGE_SIZE;
   void* idtaddr = gdtaddr - KM_PAGE_SIZE;
   int ret;

   ret = munmap(gdtaddr, KM_PAGE_SIZE);
   // we are expected to skip the monitor area and warn, but return OK
   ASSERT_EQ_FMTm("Unmap first page", 0, ret, "%d");
   ASSERT_MMAPS_COUNT(6, TOTAL_MMAPS);

   ret = munmap(idtaddr, KM_PAGE_SIZE);
   ASSERT_EQ_FMTm("Unmap first page", 0, ret, "%d");

   // mprotect should fail on 'contiguious' check
   ret = mprotect(gdtaddr, GUEST_STACK_SIZE, PROT_NONE);
   ASSERT_EQ_FMTm("change protection to NONE", -1, ret, "%d");

   void* remap_address = mremap(gdtaddr, GUEST_STACK_SIZE, GUEST_STACK_SIZE * 2, MREMAP_MAYMOVE);
   (void)remap_address;
   ASSERT_EQ_FMTm("remap to increase size", EFAULT, errno, "%d");

   ASSERT_MMAPS_COUNT(6, TOTAL_MMAPS);
   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(munmap_monitor_maps_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
