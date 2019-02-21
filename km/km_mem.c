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
 * Guest memory-related code
 */

#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "km.h"
#include "km_mem.h"
// #include "km_gdb.h"
#include "x86_cpu.h"

/*
 * Physical memory layout:
 *
 * - 4k hole
 * - 63 pages reserved area, used for pml4, pdpt, gdt, idt
 * - hole till 2MB, 0x200000
 * - User space, starting with 2MB region, each next increasing by two:
 *   2MB, 4MB, 8MB ... 512MB, 1GB, ... 256GB.
 *   We start with just 2MB region, then add the subsequent ones as necessary.
 * - hole from the end of user space to (guest_max_physmem - 1GB)
 * - last GB is reserved for stacks. The first thread stack is 2MB at the top of
 *   that GB
 *
 * We are forced to stay within width of the CPU physical memory bus. We
 * determine that the by analyzing CPUID and store in machine.guest_max_physmem
 */

static void pml4e_set(x86_pml4e_t* pml4e, uint64_t pdpt)
{
   pml4e->p = 1;
   pml4e->r_w = 1;
   pml4e->u_s = 1;
   pml4e->pdpt = pdpt >> 12;
}

static void pdpte_set(x86_pdpte_t* pdpe, uint64_t pd)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->pd = pd >> 12;
}

static void pdpte_1g_set(x86_pdpte_1g_t* pdpe, uint64_t addr)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->ps = 1;
   pdpe->page = addr >> 30;
}

static void pde_2mb_set(x86_pde_2m_t* pde, u_int64_t addr)
{
   pde->p = 1;
   pde->r_w = 1;
   pde->u_s = 1;
   pde->ps = 1;
   pde->page = addr >> 21;
}

/*
 * Virtual memory layout:
 *
 * Guest text and data region starts from virtual address 2MB. It grows and
 * shrinks at the guest requests via brk() call. The end of this region for the
 * purposes of brk() is machine.brk. The actual virtual address space is made of
 * exponentially increasing areas, staring with 2MB to the total based on
 * guest_max_physmem, allowing for stack. Note virtual and physical layout are
 * equivalent.
 *
 * TODO: update text below when code works
 *
 * Guest stack starting GUEST_STACK_START_VA, GUEST_STACK_SIZE. We chose,
 * somewhat randonly, GUEST_STACK_START_VA at the last 2MB of the first half of
 * 48 bit wide virtual address space. See km.h for values.
 *
 * We use 2MB pages for the first GB, and 1GB pages for the rest. With the
 * guest_max_physmem <= 512GB we need two pml4 entries, #0 and #255, #0 covers
 * the text and data, the #255 stack. There are correspondingly two pdpt pages.
 * We use only the very last entry in the second one. The first entry in the first one, representing
 * first 2MB of address space, it always empty. Usable memory starts from 2MB. Initially there are
 * no memory allocated, then it expands with brk.
 *
 * If we ever go over the 512GB limit there will be a need for more pdpt pages. The first entry
 * points to the pd page that covers the first GB.
 *
 * The picture illustrates layout with the constants values as set.
 * The code below is a little more flexible, allowing one or two pdpt pages and
 * pml4 entries, as well as variable locations in pdpt table, depending on the
 * virtual address of the stack
 *
 *           0|      |255
 *           [----------------] pml4 page
 *            |      |
 *         __/        \___________________
 *        /                               \
 *       |                                 |511
 *      [----------------] [----------------] pdpt pages
 *       |                                 |
 *       |\________                        |
 *       |         \                       |
 *      [----------------] [----------------] pd pages
 *        |                                |
 *      2 - 4 MB                 512GB-2MB - 512GB
 */

/* virtual address space covered by one pml4 entry */
static const uint64_t PML4E_REGION = 512 * GIB;
/* Same for pdpt entry */
static const uint64_t PDPTE_REGION = GIB;
/* same for pd entry */
static const uint64_t PDE_REGION = 2 * MIB;

static void init_pml4(km_kma_t mem)
{
   x86_pml4e_t* pml4e;
   x86_pdpte_t* pdpe;
   int idx;

   assert(machine.guest_max_physmem <= PML4E_REGION);
   pml4e = mem + RSV_PML4_OFFSET;
   memset(pml4e, 0, PAGE_SIZE);
   pml4e_set(pml4e, RSV_GUEST_PA(RSV_PDPT_OFFSET));   // entry #0

   pdpe = mem + RSV_PDPT_OFFSET;
   memset(pdpe, 0, PAGE_SIZE);
   pdpte_set(pdpe, RSV_GUEST_PA(RSV_PD_OFFSET));   // first entry for the first GB

   idx = (GUEST_MEM_TOP_VA - 1) / PML4E_REGION;
   assert(idx < PAGE_SIZE / sizeof(x86_pml4e_t));   // within pml4 page

   // check if we need the second pml4 entry, i.e the two mem regions are more
   // than 512 GB apart. If we do, make the second entry to the second pdpt page
   if (idx > 0) {
      // prepare the second pdpt page if needed
      pdpe = mem + RSV_PDPT2_OFFSET;
      memset(pdpe, 0, PAGE_SIZE);
      pml4e_set(pml4e + idx, RSV_GUEST_PA(RSV_PDPT2_OFFSET));
   }
   idx = (GUEST_MEM_TOP_VA - 1) / PDPTE_REGION & 0x1ff;
   pdpte_set(pdpe + idx, RSV_GUEST_PA(RSV_PD2_OFFSET));

   memset(mem + RSV_PD_OFFSET, 0, PAGE_SIZE);    // clear page, no usable entries
   memset(mem + RSV_PD2_OFFSET, 0, PAGE_SIZE);   // clear page, no usable entries
}

km_kma_t km_page_malloc(size_t size)
{
   km_kma_t addr;

   if ((size & (PAGE_SIZE - 1)) != 0) {
      errno = EINVAL;
      return NULL;
   }
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) ==
       MAP_FAILED) {
      return NULL;
   }
   return addr;
}

void km_page_free(km_kma_t addr, size_t size)
{
   munmap(addr, size);
}

/* simple wrapper to avoid polluting all callers with mmap.h */
km_gva_t km_guest_mmap_simple(size_t size)
{
   return km_guest_mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

/*
 * Create reserved memory, initialize PML4 and brk.
 */
void km_mem_init(void)
{
   kvm_mem_reg_t* reg;
   km_kma_t ptr;

   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      err(1, "KVM: no memory for mem region");
   }
   if ((ptr = km_page_malloc(RSV_MEM_SIZE)) == NULL) {
      err(1, "KVM: no memory for reserved pages");
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = KM_RSRV_MEMSLOT;
   reg->guest_phys_addr = RSV_MEM_START;
   reg->memory_size = RSV_MEM_SIZE;
   reg->flags = 0;   // set to KVM_MEM_READONLY for readonly
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set reserved region failed");
   }
   machine.vm_mem_regs[KM_RSRV_MEMSLOT] = reg;
   init_pml4((km_kma_t)reg->userspace_addr);

   machine.brk = GUEST_MEM_START_VA - 1;   // last allocated byte
   machine.tbrk = GUEST_MEM_TOP_VA;
   machine.guest_mid_physmem = machine.guest_max_physmem >> 1;
   machine.mid_mem_idx = MEM_IDX(machine.guest_mid_physmem - 1);
   // Place for the last 2MB of PA. We do not allocate it to make memregs mirrored
   machine.last_mem_idx = (machine.mid_mem_idx << 1) + 1;

   km_guest_mmap_init();
}

/*
 * Set and clear pml4 hierarchy entries to reflect addition or removal of reg,
 * following the setup in init_pml4(). Set/Clears pdpte or pde depending on the
 * size.
 */
static void set_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   int idx = reg->slot;
   size_t size = reg->memory_size;
   km_gva_t base = machine.vm_mem_regs[idx]->guest_phys_addr;

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde =
          km_memreg_kma(KM_RSRV_MEMSLOT) + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      base %= PDPTE_REGION;
      for (uint64_t i = 0; i < size; i += PDE_REGION) {
         pde_2mb_set(pde + (i + base) / PDE_REGION, reg->guest_phys_addr + i);
      }
   } else {
      assert(machine.pdpe1g != 0);
      x86_pdpte_1g_t* pdpe =
          km_memreg_kma(KM_RSRV_MEMSLOT) + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      for (uint64_t i = 0; i < size; i += PDPTE_REGION) {
         pdpte_1g_set(pdpe + (i + base) / PDPTE_REGION, reg->guest_phys_addr + i);
      }
   }
}

static void clear_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   int idx = reg->slot;
   uint64_t size = reg->memory_size;
   uint64_t base = memreg_base(idx);

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde =
          km_memreg_kma(KM_RSRV_MEMSLOT) + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      base %= PDPTE_REGION;
      for (uint64_t i = 0; i < size; i += PDE_REGION) {
         memset(pde + (i + base) / PDE_REGION, 0, sizeof(*pde));
      }
   } else {
      assert(machine.pdpe1g != 0);   // no 1GB pages support
      x86_pdpte_1g_t* pdpe =
          km_memreg_kma(KM_RSRV_MEMSLOT) + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      for (uint64_t i = 0; i < size; i += PDPTE_REGION) {
         memset(pdpe + (i + base) / PDPTE_REGION, 0, sizeof(*pdpe));
      }
   }
}

/*
 * Allocate memory 'size' and configure it as mem region 'idx', supporting
 * va at from idx->guest_phys_addr + va_offset.
 *
 * Return 0 for success and -errno for errors
 */
static int km_alloc_region(int idx, size_t size, int upper_va)
{
   km_kma_t ptr;
   kvm_mem_reg_t* reg;

   assert(machine.vm_mem_regs[idx] == NULL);
   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      return -ENOMEM;
   }
   if ((ptr = km_page_malloc(size)) == NULL) {
      free(reg);
      return -ENOMEM;
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = idx;
   reg->guest_phys_addr = memreg_base(idx);
   reg->memory_size = size;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      km_page_free(ptr, size);
      free(reg);
      return -errno;
   }
   machine.vm_mem_regs[idx] = reg;
   set_pml4_hierarchy(reg, upper_va);
   return 0;
}

/*
 * brk() call implementation.
 *
 * Move machine.brk up or down, adding or removing memory regions as required.
 */
km_gva_t km_mem_brk(km_gva_t brk)
{
   int idx;
   uint64_t size;
   km_gva_t m_brk;
   kvm_mem_reg_t* reg;
   km_gva_t ceiling = gva_to_gpa(machine.tbrk);   // ceiling for PA addresses avail
   int ret;

   if (brk == 0 || brk == machine.brk) {
      return machine.brk;
   }
   if (brk > ceiling) {
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.brk);
   m_brk = MIN(brk, memreg_top(idx));
   for (; m_brk < brk; m_brk = MIN(brk, memreg_top(idx))) {
      /* Not enough room in the allocated memreg, allocate and add new ones */
      idx++;
      if ((size = memreg_size(idx)) > ceiling - memreg_base(idx)) {
         return -ENOMEM;
      }
      if ((ret = km_alloc_region(idx, size, 0)) != 0) {
         return ret;
      }
   }
   for (; idx > gva_to_memreg_idx(brk); idx--) {
      /* brk moved down and left one or more memory regions. Remove and free */
      reg = machine.vm_mem_regs[idx];
      size = reg->memory_size;
      clear_pml4_hierarchy(reg, 0);
      reg->memory_size = 0;
      if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
         err(1, "KVM: failed to unplug memory region %d", idx);
      }
      km_page_free((km_kma_t)reg->userspace_addr, size);
      free(reg);
      machine.vm_mem_regs[idx] = NULL;
   }
   machine.brk = brk;
   return machine.brk;
}

// extend tbrk down. returns 0 or -errno.
int km_mmap_extend(void)
{
   int ret;
   int idx = gva_to_memreg_idx(machine.tbrk);
   int b_idx = gva_to_memreg_idx(machine.brk);

   if (idx == b_idx || (idx - 1) == b_idx) {
      /*
       * Can't allocate next slot - just move tbrk down
       * TODO  - allow  for sharing the slot and
       * make sure we don't come in the same 1GB slot
       */
      km_gva_t tbrk_pa = gva_to_gpa(machine.tbrk);
      assert(machine.brk <= tbrk_pa);
      if (machine.brk == tbrk_pa) {
         return -ENOMEM;
      }
      machine.tbrk -= (tbrk_pa - machine.brk);
      return 0;
   }
   size_t size = memreg_size(--idx);   // next idx downward
   if ((ret = km_alloc_region(idx, size, 1)) != 0) {
      return ret;
   }
   machine.tbrk -= size;
   return 0;
}
