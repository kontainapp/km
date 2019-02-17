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
 * Basic test for mmap() and friends  - WIP
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "km.h"
#include "km_mem.h"

static const uint64_t _8M = 8 * MIB;

int g_km_info_verbose;   // 0 is silent
static inline void usage()
{
   errx(1,
        "Usage: mmap [-V] "
        "\nOptions:"
        "\n\t-V      - turn on Verbose printing of internal trace messages");
}

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

int main(int argc, char* const argv[])
{
   int opt, ret;
   void* addr;
   int err_count = 0;

   while ((opt = getopt(argc, argv, "V")) != -1) {
      switch (opt) {
         case 'V':
            g_km_info_verbose = 1;
            break;
         case '?':
         default:
            usage();
      }
   }

   if ((addr = mmap(NULL, _8M, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("FAILED: mmap for size = 0x%lx - Not enough memory ", _8M);
      err_count++;
   }

   if ((ret = munmap(addr, _8M)) != 0) {
      warn("FAILED: munmap addr=%p size %s failed with %d", addr, out_sz(_8M), ret);
      err_count++;
   } else {
      warnx("Basic mmap/munmap for %s passed. addr=%p", out_sz(_8M), addr);
   }

   printf("Now map same size and then unmap half size\n");
   if ((addr = mmap(NULL, _8M, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("Not enough memory ");
      err_count++;
   }
   size_t sz = _8M / 2;
   if ((ret = munmap(addr, sz)) == 0 || errno != ENOTSUP) {
      err_count++;
      warn("FAILURE: expected ENOTSUP from munmap of %s : %d", out_sz(sz), ret);
   }

   memset(addr + sz, '2', sz);   //

   char* msg = "SUCCESS - we go to the end (%d)\n";
   if (err_count) {
      msg = "FAILED (%d)\n";
   }
   printf(msg, err_count);
   exit(err_count);
}
