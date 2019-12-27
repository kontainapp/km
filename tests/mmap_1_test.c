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
 * Test mmap/smaller mprotect in the middle/unmap sequence. Expect the maps clean after that.
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

#include "greatest/greatest.h"
#include "mmap_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "km_hcalls.h"
#include "syscall.h"

TEST mmap_overlap()
{
   static const size_t map_size = 1024 * 1024;
   static const size_t guard_size = 4 * 1024;

   if (greatest_get_verbosity() > 0) {
      printf("==== mmap_overlap\n");
   }
   void* s1 = mmap(0, map_size + 2 * guard_size, PROT_NONE, MAP_PRIVATE, -1, 0);
   mprotect(s1 + guard_size, map_size, PROT_READ | PROT_WRITE);
   munmap(s1, map_size + 2 * guard_size);
   if (greatest_get_verbosity() > 0) {
      printf("==== Pass s1=%p\n", s1);
   }
   get_maps(greatest_get_verbosity());
   ASSERT_EQ(2, info->ntotal);
   printf("total: %d free: %d\n", info->ntotal, info->nfree);
   PASS();
}

TEST mmap_mprotect_overlap()
{
   if (greatest_get_verbosity() > 0) {
      printf("==== mmap_mprotect_overlap\n");
   }
   void* s1 = mmap(0, 1 * GIB, PROT_NONE, MAP_PRIVATE, -1, 0);
   mprotect(s1 + 1 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);
   mprotect(s1 + 3 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);   // gap 1MB
   get_maps(greatest_get_verbosity());
   ASSERT_EQ(7, info->ntotal);
   mprotect(s1 + 2 * MIB, 2 * MIB, PROT_READ | PROT_WRITE);   // fill in the gap and some more
   get_maps(greatest_get_verbosity());
   mprotect(s1 + 2 * MIB, 1 * MIB, PROT_READ | PROT_WRITE);
   get_maps(greatest_get_verbosity());
   ASSERT_EQ(5, info->ntotal);

   munmap(s1, 1 * GIB);
   get_maps(greatest_get_verbosity());
   ASSERT_EQ(2, info->ntotal);
   printf("total: %d free: %d\n", info->ntotal, info->nfree);
   PASS();
}
/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(mmap_overlap);
   RUN_TEST(mmap_mprotect_overlap);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
