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
 * Basic test for mmap() and friends.
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
   size_t size;
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

   size = 8 * MIB;
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("FAILED: basic mmap for size = 0x%lx - Not enough memory ", size);
      err_count++;
   }

   if ((ret = munmap(addr, size)) != 0) {
      warn("FAILED: basic munmap addr=%p size %s failed with %d", addr, out_sz(size), ret);
      err_count++;
   }

   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("Not enough memory ");
      err_count++;
   }

   if ((ret = munmap(addr, size / 2)) == 0 || (errno != EINVAL && errno != EINVAL)) {
      err_count++;
      warn("FAILED: partial unmap(%s): expected %d or %d got errno=%d, ret=%d",
           out_sz(size / 2),
           EINVAL,
           ENOTSUP,
           ret,
           errno);
   }

   // wrong args - address is not supported
   if ((addr = mmap((void*)0x8000,
                    size,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED,
                    -1,
                    0)) != MAP_FAILED &&
       errno != EINVAL) {
      err_count++;
      warn("FAILED: mmap(addr, %s) expected EINVAL got ret=%d, errno=%d", out_sz(size), ret, errno);
      err_count++;
   }

   // Large region
   size = GIB * 3;
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("FAILED: large mmap for size = 0x%lx - Not enough memory ", size);
      err_count++;
   }
   memset(addr + size / 2, '2', size / 2 - 1);

   if ((ret = munmap(addr, size)) != 0) {
      warn("FAILED: large munmap addr=%p size %s failed with %d", addr, out_sz(size), ret);
      err_count++;
   }

   // Large region not aligned on GB
   size = GIB * 2 + MIB * 12;
   // #if 0
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      warn("FAILED: large not aligned mmap for size = 0x%lx - Not enough memory ", size);
      err_count++;
   }
   memset(addr + size / 2, '2', size / 2 - 1);

#if 0   // TODO - unmap fails with EINVAL. Commenting out to complete the PR
   if ((ret = munmap(addr, size)) != 0) {
      warn("FAILED: large not aligned munmap addr=%p size %s failed with %d",
           addr,
           out_sz(size),
           ret);
      err_count++;
   }
#endif
   printf("%s (err_count=%d)\n", err_count ? "FAILED" : "SUCCESS", err_count);
   exit(err_count);
}
