/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#ifndef __KM_MEM_H__
#define __KM_MEM_H__

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <sys/param.h>
#include <linux/kvm.h>
#include "km.h"

#define KM_PAGE_SIZE 0x1000ul   // standard 4k page
#define MIB 0x100000ul          // MByte
#define GIB 0x40000000ul        // GByte

static const int RSV_MEM_START = KM_PAGE_SIZE;
static const int RSV_MEM_SIZE = KM_PAGE_SIZE * 63;
/*
 * Mandatory data structures in reserved area to enable 64 bit CPU.
 * These numbers are offsets from the start of reserved area
 */
static const int RSV_PML4_OFFSET = 0 * KM_PAGE_SIZE;
static const int RSV_PDPT_OFFSET = 1 * KM_PAGE_SIZE;
static const int RSV_PDPT2_OFFSET = 2 * KM_PAGE_SIZE;
static const int RSV_PD_OFFSET = 3 * KM_PAGE_SIZE;
static const int RSV_PD2_OFFSET = 4 * KM_PAGE_SIZE;
static const int RSV_IDMAP_OFFSET = RSV_MEM_SIZE;   // next page after reserved area
/*
 * convert the above to guest physical offsets
 */
#define RSV_GUEST_PA(x) ((x) + RSV_MEM_START)

static const int KM_RSRV_MEMSLOT = 0;

static const km_gva_t GUEST_MEM_START_VA = 2 * MIB;
// ceiling for guest virt. address. 2MB shift down to make it aligned on GB with physical address
static const km_gva_t GUEST_MEM_TOP_VA = 128 * 1024 * GIB - 2 * MIB;

// VA offset from PA for addresses over machine.tbrk. Last 2MB of VA stay unused for symmetry.
#define GUEST_VA_OFFSET (GUEST_MEM_TOP_VA - (machine.guest_max_physmem - 2 * MIB))

/*
 * We support 2 "zones" of VAs, one on the bottom and one on the top, each no larger than this.
 * We do not support 2MB pages in the first and last GB of VA (just so we do not have to manage PDE
 * tables), So the actual zone size is 'max_physmem - GB', or just 1GB on HW with no 1g pages - all
 * VA there is provided by 2 PDP tables - see km_mem.c
 */
#define GUEST_MEM_ZONE_SIZE_VA                                                                     \
   ((machine.pdpe1g ? (machine.guest_max_physmem - GIB) : GIB) - 2 * MIB)

// We currently only have code with 1 PML4 entry per "zone", so we can't support more than that
#define GUEST_MAX_PHYSMEM_SUPPORTED 512 * GIB

// Don't support guests with less than 4GB physical
#define GUEST_MIN_PHYSMEM_SUPPORTED 4 * GIB

/*
 * See "Virtual memory layout:" in km_cpu_init.c for details.
 */
/*
 * Talking about stacks, start refers to the lowest address, top is the highest,
 * in other words start and top are view from the memory regions allocation
 * point of view.
 *
 * RSP register initially is pointed to the top of the stack, and grows down
 * with flow of the program.
 */

static const uint64_t GUEST_STACK_SIZE = 2 * MIB;   // Single thread stack size
static const uint64_t GUEST_ARG_MAX = 32 * KM_PAGE_SIZE;

/*
 * Physical address space is made of regions with size exponentially increasing from 2MB until they
 * cross the middle of the space, and then region sizes are exponentially decreasing until they
 * drop to 2MB (last region size). E.g.:
 * // clang-format off
 *  base - end       size  idx   clz  clz(end)
 *   2MB -   4MB      2MB    1    42   n/a
 *   4MB -   8MB      4MB    2    41   ..
 *   8MB -  16MB      8MB    3    40
 *  16MB -  32MB     16MB    4    39
 *     ...
 *  128GB - 256GB    128GB   17   26
 *  256GB - 384GB    128GB   18   n/a  25
 *  384GB - 448GB    64GB    19   n/a  26
 *        ...
 *  510GB - 511GB      1GB   25   n/a  32
 * // clang-format on
 *
 * idx is number of the region, we compute it based on number of leading zeroes
 * in a given address (clz) or in "512GB - address" (clz(end)), using clzl instruction. 'base' is
 * address of the first byte in it. Note size equals base in the first half of the space
 *
 * Memory regions that become guest physical memory are allocated using mmap() with specified
 * address, so that contiguous guest physical memory becomes contiguous in KM space as well. We
 * randomly choose to start guest memory allocation from 0x100000000000, which happens to be 16TB.
 * It has an advantage of being numerically the guest physical addresses with bit 0x100000000000 set.
 */
static km_kma_t KM_USER_MEM_BASE = (void*)0x100000000000ul;   // 16TiB

/*
 * Knowing memory layout and how pml4 is set, convert between guest virtual address and km address.
 */
// memreg index for an addr in the bottom half of the PA (after that the geometry changes)
static inline int MEM_IDX(km_gva_t addr)
{
   assert(addr > 0);   // clzl fails when there are no 1's
   // 43 is "64 - __builtin_clzl(2*MIB)"
   return (43 - __builtin_clzl(addr));
}

// guest virtual address to guest physical address - adjust high gva down
static inline km_gva_t gva_to_gpa_nocheck(km_gva_t gva)
{
   if (gva > GUEST_VA_OFFSET) {
      gva -= GUEST_VA_OFFSET;
   }
   return gva;
}

static inline km_gva_t gva_to_gpa(km_gva_t gva)
{
   // gva must be in the bottom or top "max_physmem", but not in between
   assert((GUEST_MEM_START_VA - 1 <= gva && gva < machine.guest_max_physmem) ||
          (GUEST_VA_OFFSET <= gva && gva <= GUEST_MEM_TOP_VA));
   return gva_to_gpa_nocheck(gva);
}

// helper to convert physical to virtual in the top of VA
static inline km_gva_t gpa_to_upper_gva(uint64_t gpa)
{
   assert(GUEST_MEM_START_VA <= gpa && gpa < machine.guest_max_physmem);
   return gpa + GUEST_VA_OFFSET;
}

static inline int gva_to_memreg_idx(km_gva_t addr)
{
   addr = gva_to_gpa(addr);   // adjust for gva in the top part of VA space
   if (addr <= machine.guest_mid_physmem) {
      return MEM_IDX(addr);
   }
   return machine.last_mem_idx - MEM_IDX(machine.guest_max_physmem - addr - 1);
}

static inline km_gva_t memreg_top(int idx);   // forward declaration

/* memreg_base() and memreg_top() return guest physical addresses */
static inline uint64_t memreg_base(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return MIB << idx;
   }
   return machine.guest_max_physmem - memreg_top(machine.last_mem_idx - idx);
}

static inline km_gva_t memreg_top(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return (MIB << 1) << idx;
   }
   return machine.guest_max_physmem - memreg_base(machine.last_mem_idx - idx);
}

static inline uint64_t memreg_size(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return MIB << idx;
   }
   return MIB << (machine.last_mem_idx - idx);
}

/*
 * Translates guest virtual address to km address, assuming the guest address is valid.
 * To be used to make it obvious that gva is *known* to be valid.
 * @param gva Guest virtual address
 * @returns Address in KM
 */
static inline km_kma_t km_gva_to_kma_nocheck(km_gva_t gva)
{
   return KM_USER_MEM_BASE + gva_to_gpa_nocheck(gva);
}

/*
 * Translates guest virtual address to km address, checking for validity.
 * @param gva Guest virtual address
 * @returns Address in KM. returns NULL if guest VA is invalid.
 */
static inline km_kma_t km_gva_to_kma(km_gva_t gva)
{
   if (gva < GUEST_MEM_START_VA ||
       (roundup(machine.brk, KM_PAGE_SIZE) <= gva && gva < rounddown(machine.tbrk, KM_PAGE_SIZE)) ||
       GUEST_MEM_TOP_VA < gva) {
      return NULL;
   }
   return km_gva_to_kma_nocheck(gva);
}

void km_mem_init(km_machine_init_params_t* params);
void km_guest_page_free(km_gva_t addr, size_t size);
void km_guest_mmap_init(void);
km_gva_t km_mem_brk(km_gva_t brk);
km_gva_t km_mem_tbrk(km_gva_t tbrk);
km_gva_t km_guest_mmap_simple(size_t stack_size);
km_gva_t km_guest_mmap_simple_monitor(size_t stack_size);
km_gva_t km_guest_mmap(km_gva_t addr, size_t length, int prot, int flags, int fd, off_t offset);
int km_guest_munmap(km_gva_t addr, size_t length);
km_gva_t km_guest_mremap(km_gva_t old_address, size_t old_size, size_t new_size, int flags, ...);
int km_guest_mprotect(km_gva_t addr, size_t size, int prot);

#endif /* #ifndef __KM_MEM_H__ */
