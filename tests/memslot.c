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
 * Test helper to print out misc. info about memory slots in different memory sizes
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "km.h"
#include "km_mem.h"

km_machine_t machine;

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

#define CHECK(expr) ((expr) ? 0 : check_fail(#expr, __FILE__, __LINE__))
// returns 1 to count errors
static inline int check_fail(char* expr, char* file, int line)
{
   printf("Check '%s' failed at %s:%d\n", expr, file, line);
   return 1;
}

int main(int argc, char** argv)
{
   int size_in_gb = 512;   // ph. mem size
   int err_count = 0;

   if (argc == 2) {
      size_in_gb = atoi(argv[1]);
      printf("ok\n");
   }
   machine.guest_max_physmem = size_in_gb * GIB;

   // TBD: get from km_mem_init
   machine.brk = GUEST_MEM_START_VA - 1;   // last allocated byte
   machine.tbrk = GUEST_MEM_TOP_VA;
   machine.guest_mid_physmem = machine.guest_max_physmem >> 1;
   machine.mid_mem_idx = MEM_IDX(machine.guest_mid_physmem - 1);
   // Place for the last 2MB of PA. We do not allocate it to make memregs mirrored
   machine.last_mem_idx = (machine.mid_mem_idx << 1) + 1;

   printf("Modelling %s memory PA, alloc mirror at %s\n",
          out_sz(machine.guest_max_physmem),
          out_sz(machine.guest_mid_physmem));
   printf("last_slot %d, mid %d\n", machine.last_mem_idx, machine.mid_mem_idx);
   printf(
       "   start(hex)      START          SIZE         clz  clz-r  idx memreg-base  memreg-size  "
       "memreg-top\n");

   int64_t current_size, current_start;
   for (current_size = current_start = 2 * MIB;
        current_start < machine.guest_max_physmem - 2 * MIB;) {
      int clz = __builtin_clzl(current_start);
      int idx = gva_to_memreg_idx(current_start);
      // calculate start / size for the next step
#if 1
      printf("0x%-12lx %12s %12s\t%3d %3d %3d %12s %12s %12s\n",
             current_start,
             out_sz(current_start),
             out_sz(current_size),
             clz,
             __builtin_clzl(machine.guest_max_physmem - current_start),
             idx,
             out_sz(memreg_base(idx)),
             out_sz(memreg_size(idx)),
             out_sz(memreg_top(idx)));
#endif
      err_count += CHECK(current_start == memreg_base(idx));
      err_count += CHECK(current_size == memreg_size(idx));
      err_count += CHECK(current_size + current_start == memreg_top(idx));
      // printf("{0x%lx, %d},\n", current_start, idx);
      current_start += current_size;
      if (current_start > machine.guest_mid_physmem && current_size > 2 * MIB) {
         current_size /= 2;
      } else if (current_start < machine.guest_mid_physmem) {
         current_size *= 2;
      }   // do nothing on mirror
   }
   return err_count;
}
