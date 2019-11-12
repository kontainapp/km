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

#include "../km/km_unittest.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "syscall.h"

static const int MAX_MAPS = 4096;
static km_ut_get_mmaps_t* info;
static km_mmap_reg_t saved_entry;

static int get_maps(bool verify)
{
   int ret;

   if (info == NULL) {
      info = calloc(1, sizeof(km_ut_get_mmaps_t) + MAX_MAPS * sizeof(km_mmap_reg_t));
      assert(info != NULL);
   }
   info->ntotal = MAX_MAPS;
   if ((ret = syscall(HC_km_unittest, KM_UT_GET_MMAPS_INFO, info)) == -EAGAIN) {
      printf("WOW, Km reported too many maps: %d", info->ntotal);
      return -1;
   }
   if (ret == -ENOTSUP) {
      printf("Km reported ioctl not supported\n");
      return -1;
   }
   ASSERT_EQ_FMTm("verify total map count", 1, info->ntotal, "%d");
   ASSERT_EQ_FMTm("verify free map count", 0, info->nfree, "%d");
   if (verify == false) {
      saved_entry = info->maps[0];
   } else {
      if ((info->maps[0].start != saved_entry.start) || (info->maps[0].size != saved_entry.size) ||
          (info->maps[0].flags != saved_entry.flags) ||
          (info->maps[0].km_flags != saved_entry.km_flags)) {
         return 1;
      }
   }
   return 0;
}

TEST munmap_monitor_maps_test(void)
{
   void* gdtaddr = (void*)GUEST_MEM_TOP_VA - KM_PAGE_SIZE;
   void* idtaddr = gdtaddr - KM_PAGE_SIZE;
   void* userstack = idtaddr - GUEST_STACK_SIZE;

   ASSERT_EQ_FMTm("Fetch initial maps", 0, get_maps(false), "%d");

   ASSERT_EQ_FMTm("Unmap first page", 0, munmap(gdtaddr, KM_PAGE_SIZE), "%d");
   ASSERT_EQ_FMTm("Verify after first unmap", 0, get_maps(true), "%d");

   ASSERT_EQ_FMTm("Unmap first page", 0, munmap(idtaddr, KM_PAGE_SIZE), "%d");
   ASSERT_EQ_FMTm("Verify after second unmap", 0, get_maps(true), "%d");

   ASSERT_EQ_FMTm("Unmap first page", 0, munmap(userstack, GUEST_STACK_SIZE), "%d");
   ASSERT_EQ_FMTm("Verify after third unmap", 0, get_maps(true), "%d");

   ASSERT_EQ_FMTm("change protection to NONE", 0, mprotect(userstack, GUEST_STACK_SIZE, PROT_NONE), "%d");
   ASSERT_EQ_FMTm("Verify after mprotect", 0, get_maps(true), "%d");

   void *remap_address = mremap(userstack, GUEST_STACK_SIZE, GUEST_STACK_SIZE * 2, MREMAP_MAYMOVE);
   (void)remap_address;
   ASSERT_EQ_FMTm("remap to increase size", EFAULT, errno, "%d");
   ASSERT_EQ_FMTm("Verify after mprotect", 0, get_maps(true), "%d");

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
