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
 */

#ifndef __KM_MEM_H__
#define __KM_MEM_H__

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <sys/param.h>
#include <linux/kvm.h>
#include "km.h"

static const uint64_t PAGE_SIZE = 0x1000;   // standard 4k page
static const uint64_t MIB = 0x100000;       // MByte
static const uint64_t GIB = 0x40000000ul;   // GByte

static const int RSV_MEM_START = PAGE_SIZE;
static const int RSV_MEM_SIZE = PAGE_SIZE * 63;
/*
 * Mandatory data structures in reserved area to enable 64 bit CPU.
 * These numbers are offsets from the start of reserved area
 */
static const int RSV_PML4_OFFSET = 0 * PAGE_SIZE;
static const int RSV_PDPT_OFFSET = 1 * PAGE_SIZE;
static const int RSV_PDPT2_OFFSET = 2 * PAGE_SIZE;
static const int RSV_PD_OFFSET = 3 * PAGE_SIZE;
static const int RSV_PD2_OFFSET = 4 * PAGE_SIZE;
/*
 * convert the above to guest physical offsets
 */
#define RSV_GUEST_PA(x) ((x) + RSV_MEM_START)

static const int KM_RSRV_MEMSLOT = 0;
static const int KM_TEXT_DATA_MEMSLOT = 1;
// other text and data memslots follow

// memreg_base(KM_TEXT_DATA_MEMSLOT)
static const km_gva_t GUEST_MEM_START_VA = 2 * MIB;
// ceiling for guest virt. address. 2MB shift down to make it aligned on GB with physical address
static const km_gva_t GUEST_MEM_TOP_VA = 128 * 1024 * GIB - 2 * MIB;

// VA offset from PA for addresses over machine.tbrk. Last 2MB of VA stay unused for symmetry.
#define GUEST_VA_OFFSET (GUEST_MEM_TOP_VA - (machine.guest_max_physmem - 2 * MIB))
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
static const uint64_t GUEST_ARG_MAX = 32 * PAGE_SIZE;

/*
 * Physical address space is made of regions with size exponentially increasing from 2MB until they
 * cross the middle of the space ,  and then region sizes are exponentially decreasing until they
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
 * Knowing memory layout and how pml4 is set, convert between guest virtual address and km address.
 *
 * Note 1:
 * The actual virtual address space is made of areas mapped to guest physical memory regions
 * described above. KM virtual memory (backing guest phys. memory) is contigious only within a
 * region.
 */

// memreg index for an addr in the bottom half of the PA (after that the geometry changes)
static inline int MEM_IDX(km_gva_t addr)
{
   // 43 is "64 - __builtin_clzl(2*MIB)"
   return (43 - __builtin_clzl(addr));
}

// guest virtual address to guest physical address - adjust high gva down
static inline km_gva_t gva_to_gpa(km_gva_t gva)
{
   assert((GUEST_MEM_START_VA - 1 <= gva && gva < machine.guest_max_physmem) ||
          (GUEST_VA_OFFSET <= gva && gva <= GUEST_MEM_TOP_VA));
   if (gva > GUEST_VA_OFFSET) {
      gva -= GUEST_VA_OFFSET;
   }
   return gva;
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

static inline km_kma_t km_gva_to_kma(km_gva_t gva)
{
   if ((GUEST_MEM_START_VA <= gva && gva < GUEST_MEM_TOP_VA)) {
      int i = gva_to_memreg_idx(gva);
      return machine.vm_mem_regs[i].memory_size == 0
                 ? NULL
                 : gva_to_gpa(gva) - memreg_base(i) + (void*)machine.vm_mem_regs[i].userspace_addr;
   }
   return NULL;
}

void km_mem_init(void);
void km_page_free(void* addr, size_t size);
void* km_page_malloc(size_t size);
void km_guest_mmap_init(void);
km_gva_t km_mem_brk(km_gva_t brk);
km_gva_t km_mem_tbrk(km_gva_t tbrk);
km_gva_t km_guest_mmap_simple(size_t stack_size);
km_gva_t km_guest_mmap(km_gva_t addr, size_t length, int prot, int flags, int fd, off_t offset);
int km_guest_munmap(km_gva_t addr, size_t length);
km_gva_t km_guest_mremap(km_gva_t old_address, size_t old_size, size_t new_size, int flags, ...);

#endif /* #ifndef __KM_MEM_H__ */