/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "km.h"
#include "km_mem.h"
#include "km_guest.h"
#include "km_proc.h"
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

static inline km_kma_t km_resv_kma(void)
{
   return (km_kma_t)machine.vm_mem_regs[KM_RSRV_MEMSLOT].userspace_addr;
}

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
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->ps = 1;
   pdpe->glb = 1;
   pdpe->page = addr >> 30;
}

static void pde_2mb_set(x86_pde_2m_t* pde, u_int64_t addr)
{
   pde->r_w = 1;
   pde->u_s = 1;
   pde->ps = 1;
   pde->glb = 1;
   pde->page = addr >> 21;
}

static void pde_4k_set(x86_pde_4k_t* pde, uint64_t addr)
{
   pde->p = 1;
   pde->r_w = 1;
   pde->u_s = 1;
   pde->pta = addr >> 12;
}

static void pte_set(x86_pte_4k_t* pte, uint64_t addr)
{
   pte->p = 1;
   pte->r_w = 1;
   pte->u_s = 1;
   pte->page = addr >> 12;
}

/*
 * Virtual Memory layout:
 *
 * Virtual address space starts from virtual address GUEST_MEM_START_VA (2MB) and ends at virtual
 * address GUEST_MEM_TOP_VA which is chosen to be, somewhat randonly, at the last 2MB of the
 * first half of 48 bit wide virtual address space. See km.h for values.
 *
 * The virtual address space contains 2 regions - bottom, growing up (for guest
 * text and data) and top, growing down (for stacks and mmap-ed regions).
 *
 * Bottom region: guest text and data region starts from the beginning of the virtual address space.
 * It grows (upwards) and shrinks at the guest requests via brk() call. The end of this region for
 * the purposes of brk() is machine.brk.
 *
 * Top region: stack and mmap-allocated region starts from the end of the virtual address space. It
 * grows (downwards) and shrinks at the guest requests via mmap() call, as well as Monitor requests
 * via km_guest_mmap_simple() call, usually used for stack(s) allocation. The end (i.e. the lowest
 * address) of this region is machine.tbrk.
 *
 * Total amount of guest virtual memory (bottom region + top region) is currently limited to
 * 'guest_max_physical_mem-4MB' (i.e. 512GB-4MB). Virtual to physical is essentially 1:1
 * mapping, but the top region virtual addresses are equal to physical_addresses + GUEST_VA_OFFSET.
 *
 * We use 2MB pages for the first and last GB of virtual space, and 1GB pages for the rest. With the
 * guest_max_physmem == 512GB we need two pml4 entries, #0 and #255. #0 covers the text and data (up
 * to machine.brk), the #255 mmap and stack area (from machine.tbrk up). There are correspondingly two
 * pdpt pages. Since we allow 2MB pages only for the first and last GB, we use only 2 pdp tables. The
 * rest of VA is covered by 1GB-wide slots in relevant PDPT. The first entry in the
 * first PDP table, representing first 2MB of address space, it always empty. Same for the last
 * entry in the second PDP table - it is always empty. Usable virtual memory starts from 2MB and
 * ends at GUEST_MEM_TOP_VA-2MB. Usable physical memory starts at 2MB and ends at 512GB-2MB.
 * Initially there is no memory allocated, then it expands with brk (left to right in the picture
 * below) or with mmap (right to left).
 *
 * If we ever go over the 512GB limit there will be a need for more pdpt pages. The first entry
 * points to the pd page that covers the first GB.
 *
 * The picture illustrates layout with the constants values as set.
 * The code below is a little more flexible, allowing one or two pdpt pages and
 * pml4 entries, as well as variable locations in pdpt table
 *
 * // clang-format off
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
 *      2 - 4 MB                 512GB-2MB - 512GB    <==== physical addresses
 *
 * // clang-format on
 */

/* virtual address space covered by one pml4 entry */
static const uint64_t PML4E_REGION = 512 * GIB;
/* Same for pdpt entry */
static const uint64_t PDPTE_REGION = GIB;
/* same for pd entry */
static const uint64_t PDE_REGION = 2 * MIB;

/*
 * Slot number in a page table for gva
 */
static inline int PTE_SLOT(km_gva_t __addr)
{
   return (__addr >> 12) & 0x1ff;
}

/*
 * Slot number in PDE table for gva '_addr'.
 * Logically: (__addr % PDPTE_REGION) / PDE_REGION
 */
static inline int PDE_SLOT(km_gva_t __addr)
{
   return (__addr >> 21) & 0x1ff;
}

/*
 * Slot number for 1G chunk in PDPT table for gva '_addr'
 * Logically:  (__addr % PML4E_REGION) / PDPTE_REGION
 */
static inline int PDPTE_SLOT(km_gva_t __addr)
{
   return ((__addr)&0x7ffffffffful) >> 30;
}

/*
 * Remember these payload virtual addresses for placement in auxv[].
 * Base of [vvar] pages in [0]
 * Base of [vdso] pages in [1].
 */
km_gva_t km_vvar_vdso_base[2];
uint32_t km_vvar_vdso_size;

/*
 * Put the [vvar] and [vdso] pages from km's address space into the payload's
 * physical and virtual address spaces.
 */
static void km_add_vvar_vdso_to_guest_address_space(km_kma_t mem)
{
   maps_region_t vvar_vdso_regions[vvar_vdso_regions_count] =
       {{.name_substring = "[vvar]"},
        {.name_substring = "[vdso]"}};   // the order of these entries is important.  don't change.
   kvm_mem_reg_t* reg;
   int rc;

   // Get the km addresses of vvar and vdso pages
   rc = km_find_maps_regions(vvar_vdso_regions, vvar_vdso_regions_count);
   if (rc != 0) {
      km_infox(KM_TRACE_MEM, "Couldn't find vvar/vdso memory segments, not using vdso");
      return;
   }

   // This code assumes [vvar] and [vdso] are adjacent.
   assert(vvar_vdso_regions[vvar_region_index].end_addr ==
          vvar_vdso_regions[vdso_region_index].begin_addr);

   km_vvar_vdso_size = (vvar_vdso_regions[vvar_region_index].end_addr -
                        vvar_vdso_regions[vvar_region_index].begin_addr) +
                       (vvar_vdso_regions[vdso_region_index].end_addr -
                        vvar_vdso_regions[vdso_region_index].begin_addr);

   // Map vvar and vdso at this guest virtual address
   km_vvar_vdso_base[0] = GUEST_VVAR_VDSO_BASE_VA;
   km_vvar_vdso_base[1] =
       km_vvar_vdso_base[0] + (vvar_vdso_regions[0].end_addr - vvar_vdso_regions[0].begin_addr);

   // Put the vdso and vvar pages into the payload's physical address space.
   reg = &machine.vm_mem_regs[KM_RSRV_VDSOSLOT];
   reg->slot = KM_RSRV_VDSOSLOT;
   reg->userspace_addr = (typeof(reg->userspace_addr))vvar_vdso_regions[0].begin_addr;
   reg->guest_phys_addr = gva_to_gpa_nocheck(km_vvar_vdso_base[0]);
   reg->memory_size = km_vvar_vdso_size;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set vvar/vdso region failed");
   }

   // Now add vdso and vvar to the payload's virtual address space
   int idx;
   uint64_t virtaddr = km_vvar_vdso_base[0];
   uint64_t physaddr = virtaddr - GUEST_VA_OFFSET;
   x86_pde_4k_t* pde = mem + RSV_PD2_OFFSET;
   x86_pte_4k_t* pte = mem + RSV_PT_OFFSET;

   // add entry to page directory
   idx = PDE_SLOT(virtaddr);
   pde_4k_set(pde + idx, RSV_GUEST_PA(RSV_PT_OFFSET));

   // add vvar and vdso pages to page table
   memset(mem + RSV_PT_OFFSET, 0, KM_PAGE_SIZE);   // clear page, no usable entries yet
   for (int i = 0; i < vvar_vdso_regions_count; i++) {
      km_infox(KM_TRACE_MEM,
               "%s: km vaddr 0x%lx, payload paddr 0x%lx, payload vaddr 0x%lx",
               vvar_vdso_regions[i].name_substring,
               vvar_vdso_regions[i].begin_addr,
               physaddr,
               virtaddr);
      for (uint64_t b = vvar_vdso_regions[i].begin_addr; b < vvar_vdso_regions[i].end_addr;
           b += KM_PAGE_SIZE) {
         idx = PTE_SLOT(virtaddr);
         pte_set(pte + idx, physaddr);
         virtaddr += KM_PAGE_SIZE;
         physaddr += KM_PAGE_SIZE;
      }
   }

   rc = km_monitor_pages_in_guest(km_vvar_vdso_base[0],
                                  vvar_vdso_regions[0].end_addr - vvar_vdso_regions[0].begin_addr,
                                  PROT_READ,
                                  "[vvar]");
   assert(rc == 0);
   rc = km_monitor_pages_in_guest(km_vvar_vdso_base[1],
                                  vvar_vdso_regions[1].end_addr - vvar_vdso_regions[1].begin_addr,
                                  PROT_EXEC,
                                  "[vdso]");
   assert(rc == 0);
}

/*
 * Insert the km_guest code that resides in the km address space into the
 * guest's physical and virtual address spaces.
 */
static void km_add_code_to_guest_address_space(void)
{
   kvm_mem_reg_t* reg;
   km_gva_t virtaddr = GUEST_KMGUESTMEM_BASE_VA;
   uint64_t physaddr = gva_to_gpa_nocheck(virtaddr);
   int idx;

   // km_guest pages must start on a page boundary and must be a multiple of the page size in length.
   assert(((uint64_t)&km_guest_start & (KM_PAGE_SIZE - 1)) == 0);
   assert((((uint64_t)&km_guest_end - (uint64_t)&km_guest_end) & (KM_PAGE_SIZE - 1)) == 0);

   // Map the km_guest pages into the guest physical address space
   reg = &machine.vm_mem_regs[KM_RSRV_KMGUESTMEM_SLOT];
   reg->slot = KM_RSRV_KMGUESTMEM_SLOT;
   reg->userspace_addr = (uint64_t)&km_guest_start;
   reg->memory_size = &km_guest_end - &km_guest_start;
   reg->guest_phys_addr = physaddr;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set km_guest mem region failed");
   }

   /*
    * We know this is the page table the km_guest page addresses will be placed into.
    * If you change the value of GUEST_KMGUESTMEM_BASE_VA be sure you verify that we
    * are updating the correct page table.
    */
   x86_pte_4k_t* pte = (x86_pte_4k_t*)(km_resv_kma() + RSV_PT_OFFSET);

   // Add the km_guest pages into the guest virtual address space
   for (uint8_t* p = &km_guest_start; p < &km_guest_end; p += KM_PAGE_SIZE) {
      idx = PTE_SLOT(virtaddr);
      pte_set(pte + idx, physaddr);
      virtaddr += KM_PAGE_SIZE;
      physaddr += KM_PAGE_SIZE;
   }

   /*
    * Put the km_guest pages into the busy memory list so they will be included in
    * core dumps and /proc/pid/maps.
    */
   int rc;
   rc = km_monitor_pages_in_guest(GUEST_KMGUESTMEM_BASE_VA,
                                  &km_guest_data_start - &km_guest_start,
                                  PROT_EXEC,
                                  "[km_guest_text]");
   assert(rc == 0);
   rc = km_monitor_pages_in_guest(GUEST_KMGUESTMEM_BASE_VA + (&km_guest_data_start - &km_guest_start),
                                  &km_guest_end - &km_guest_data_start,
                                  PROT_READ,
                                  "[km_guest_data]");
   assert(rc == 0);
}

static void init_pml4(km_kma_t mem)
{
   x86_pml4e_t* pml4e;
   x86_pdpte_t* pdpe;
   int idx;

   // The code assumes that a single PML4 slot can cover all available physical memory.
   assert(machine.guest_max_physmem <= PML4E_REGION);
   // The code assumes that PA and VA are aligned within PDE table (which covers PDPTE_REGION)
   assert(GUEST_VA_OFFSET % PDPTE_REGION == 0);

   pml4e = mem + RSV_PML4_OFFSET;
   memset(pml4e, 0, KM_PAGE_SIZE);
   pml4e_set(pml4e, RSV_GUEST_PA(RSV_PDPT_OFFSET));   // entry #0

   pdpe = mem + RSV_PDPT_OFFSET;
   memset(pdpe, 0, KM_PAGE_SIZE);
   pdpte_set(pdpe, RSV_GUEST_PA(RSV_PD_OFFSET));   // first entry for the first GB

   // Since in our mem layout the two mem regions are always more
   // than 512 GB apart, make the second pml4 entry pointing to the second pdpt page
   idx = (GUEST_MEM_TOP_VA - 1) / PML4E_REGION;
   pml4e_set(pml4e + idx, RSV_GUEST_PA(RSV_PDPT2_OFFSET));
   pdpe = mem + RSV_PDPT2_OFFSET;
   memset(pdpe, 0, KM_PAGE_SIZE);

   idx = PDE_SLOT(GUEST_MEM_TOP_VA);
   pdpte_set(pdpe + idx, RSV_GUEST_PA(RSV_PD2_OFFSET));

   // Clear the 2 page directory pages
   memset(mem + RSV_PD_OFFSET, 0, KM_PAGE_SIZE);    // clear page, no usable entries
   memset(mem + RSV_PD2_OFFSET, 0, KM_PAGE_SIZE);   // clear page, no usable entries
}

static int overcommit_memory;   // controls how we request memory for payload from Linux

static void* km_guest_page_malloc(km_gva_t gpa_hint, size_t size, int prot)
{
   km_kma_t addr;
   int flags = MAP_PRIVATE | MAP_ANONYMOUS | (overcommit_memory == 1 ? MAP_NORESERVE : 0);

   if ((size & (KM_PAGE_SIZE - 1)) != 0 || (gpa_hint & (KM_PAGE_SIZE - 1)) != 0) {
      errno = EINVAL;
      return NULL;
   }
   if ((addr = mmap(gpa_hint + KM_USER_MEM_BASE, size, prot, flags, -1, 0)) == MAP_FAILED) {
      return NULL;
   }
   if (addr != gpa_hint + KM_USER_MEM_BASE) {
      errx(1, "Problem getting guest memory, wanted %p, got %p", gpa_hint + KM_USER_MEM_BASE, addr);
   }
   return addr;
}

void km_guest_page_free(km_gva_t addr, size_t size)
{
   munmap(addr + KM_USER_MEM_BASE, size);
}

/*
 * Create reserved memory, initialize PML4 and brk.
 */
void km_mem_init(km_machine_init_params_t* params)
{
   kvm_mem_reg_t* reg;
   void* ptr;

   overcommit_memory = (params->overcommit_memory == KM_FLAG_FORCE_ENABLE);
   reg = &machine.vm_mem_regs[KM_RSRV_MEMSLOT];
   if ((ptr = km_guest_page_malloc(RSV_MEM_START, RSV_MEM_SIZE, PROT_READ | PROT_WRITE)) == NULL) {
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
   /*
    * Move identity map page out of the way. It only gets used if unrestricted_guest support is off,
    * but we need to make sure our memory regions don't overlap with it
    */
   uint64_t idmap = RSV_GUEST_PA(RSV_IDMAP_OFFSET);
   if (ioctl(machine.mach_fd, KVM_SET_IDENTITY_MAP_ADDR, &idmap) < 0) {
      err(1, "KVM: set identity map addr failed");
   }
   init_pml4((km_kma_t)reg->userspace_addr);

   machine.brk = GUEST_MEM_START_VA;
   machine.tbrk = GUEST_MEM_TOP_VA;
   machine.guest_mid_physmem = machine.guest_max_physmem >> 1;
   machine.mid_mem_idx = MEM_IDX(machine.guest_mid_physmem - 1);
   // Place for the last 2MB of PA. We do not allocate it to make memregs mirrored
   machine.last_mem_idx = (machine.mid_mem_idx << 1) + 1;
   km_guest_mmap_init();

   // Add the [vvar] and [vdso] pages from km into the physical and virtual address space for the payload
   km_add_vvar_vdso_to_guest_address_space((km_kma_t)reg->userspace_addr);

   // Map guest code resident in km into the guest's address space
   km_add_code_to_guest_address_space();
}

void km_mem_fini(void)
{
   km_guest_mmap_fini();
}

#define MAX_PDE_SLOT PDE_SLOT(GUEST_MEM_TOP_VA)
#define MAX_PDPTE_SLOT PDPTE_SLOT(GUEST_MEM_TOP_VA)

/*
 * fixup_bottom_page_tables() and fixup_top_page_tables() deal
 * with symmetric virtual memory layouts, so be aware that changes
 * to one typically require changes to the other.
 */
static inline void fixup_bottom_page_tables(km_gva_t old_brk, km_gva_t new_brk)
{
   x86_pde_2m_t* pde = km_resv_kma() + RSV_PD_OFFSET;
   x86_pdpte_1g_t* pdpe = km_resv_kma() + RSV_PDPT_OFFSET;
   int old_2m_slot = PDE_SLOT(old_brk);
   int new_2m_slot = PDE_SLOT(new_brk);
   int old_1g_slot = PDPTE_SLOT(old_brk);
   int new_1g_slot = PDPTE_SLOT(new_brk);

   if (new_brk > old_brk) {
      km_infox(KM_TRACE_MEM,
               "grow  old:0x%lx(%d,%d) new:0x%lx(%d,%d)",
               old_brk,
               old_2m_slot,
               old_1g_slot,
               new_brk,
               new_2m_slot,
               new_1g_slot);
      if (old_1g_slot == 0) {
         int limit = (new_1g_slot == 0) ? new_2m_slot : MAX_PDE_SLOT;
         for (int i = old_2m_slot; i <= limit; i++) {
            pde[i].p = 1;
         }
      }
      if (new_1g_slot > 0) {
         int start = (old_1g_slot == 0) ? 1 : old_1g_slot;
         for (int i = start; i <= new_1g_slot; i++) {
            pdpe[i].p = 1;
         }
      }
   } else {
      km_infox(KM_TRACE_MEM,
               "shrink old:0x%lx(%d,%d) new:0x%lx(%d,%d)",
               old_brk,
               old_2m_slot,
               old_1g_slot,
               new_brk,
               new_2m_slot,
               new_1g_slot);
      if (old_1g_slot > 0) {
         int start = (new_1g_slot == 0) ? 1 : new_1g_slot;
         for (int i = start; i < old_1g_slot; i++) {
            pdpe[i].p = 0;
         }
      }
      if (new_1g_slot == 0) {
         int limit = (old_1g_slot == 0) ? old_2m_slot : MAX_PDE_SLOT;
         for (int i = new_2m_slot + 1; i <= limit; i++) {
            pde[i].p = 0;
         }
      }
   }
}

static inline void fixup_top_page_tables(km_gva_t old_brk, km_gva_t new_brk)
{
   x86_pde_2m_t* pde = km_resv_kma() + RSV_PD2_OFFSET;
   x86_pdpte_1g_t* pdpe = km_resv_kma() + RSV_PDPT2_OFFSET;
   int old_2m_slot = PDE_SLOT(old_brk);
   int new_2m_slot = PDE_SLOT(new_brk);
   int old_1g_slot = PDPTE_SLOT(old_brk);
   int new_1g_slot = PDPTE_SLOT(new_brk);

   if (new_brk < old_brk) {
      km_infox(KM_TRACE_MEM,
               "grow old:0x%lx(%d,%d) new:0x%lx(%d,%d)",
               old_brk,
               old_2m_slot,
               old_1g_slot,
               new_brk,
               new_2m_slot,
               new_1g_slot);
      if (old_1g_slot == MAX_PDPTE_SLOT) {
         int start = (new_1g_slot == MAX_PDPTE_SLOT) ? new_2m_slot : 0;
         for (int i = start; i <= old_2m_slot; i++) {
            pde[i].p = 1;
         }
      }
      if (new_1g_slot < MAX_PDPTE_SLOT) {
         for (int i = new_1g_slot; i <= old_1g_slot; i++) {
            pdpe[i].p = 1;
         }
      }
   } else {
      km_infox(KM_TRACE_MEM,
               "shrink old:0x%lx(%d,%d) new:0x%lx(%d,%d)",
               old_brk,
               old_2m_slot,
               old_1g_slot,
               new_brk,
               new_2m_slot,
               new_1g_slot);
      if (old_1g_slot < MAX_PDPTE_SLOT) {
         for (int i = old_1g_slot; i < new_1g_slot; i++) {
            pdpe[i].p = 0;
         }
      }
      if (new_1g_slot == MAX_PDPTE_SLOT) {
         int start = (old_1g_slot == MAX_PDPTE_SLOT) ? 0 : old_2m_slot;
         for (int i = start; i < new_2m_slot; i++) {
            pde[i].p = 0;
         }
      }
   }
}

/*
 * Set and clear pml4 hierarchy entries to reflect addition or removal of reg,
 * following the setup in init_pml4(). Set/Clears pdpte or pde depending on the
 * size.
 */
static void set_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   size_t size = reg->memory_size;
   km_gva_t base = reg->guest_phys_addr;

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_resv_kma() + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      for (uint64_t addr = base; addr < base + size; addr += PDE_REGION) {
         // virtual and physical mem aligned the same on PDE_REGION, so we can use use addr for virt.addr
         pde_2mb_set(pde + PDE_SLOT(addr), addr);
      }
   } else {
      assert(machine.pdpe1g != 0);
      x86_pdpte_1g_t* pdpe = km_resv_kma() + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      uint64_t gva = upper_va ? gpa_to_upper_gva(base) : base;
      for (uint64_t addr = gva; addr < gva + size; addr += PDPTE_REGION, base += PDPTE_REGION) {
         pdpte_1g_set(pdpe + PDPTE_SLOT(addr), base);
      }
   }
}

static void clear_pml4_hierarchy(kvm_mem_reg_t* reg, int upper_va)
{
   uint64_t size = reg->memory_size;
   uint64_t base = reg->guest_phys_addr;

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_resv_kma() + (upper_va ? RSV_PD2_OFFSET : RSV_PD_OFFSET);
      for (uint64_t addr = base; addr < base + size; addr += PDE_REGION) {
         // virtual and physical mem aligned the same on PDE_REGION, so we can use phys. address in
         // PDE_SLOT()
         memset(pde + PDE_SLOT(addr), 0, sizeof(*pde));
      }
   } else {
      assert(machine.pdpe1g != 0);   // no 1GB pages support
      x86_pdpte_1g_t* pdpe = km_resv_kma() + (upper_va ? RSV_PDPT2_OFFSET : RSV_PDPT_OFFSET);
      uint64_t gva = upper_va ? gpa_to_upper_gva(base) : base;
      for (uint64_t addr = gva; addr < gva + size; addr += PDPTE_REGION, base += PDPTE_REGION) {
         memset(pdpe + PDPTE_SLOT(addr), 0, sizeof(*pdpe));
      }
   }
}

/*
 * Allocate memory 'size' and configure it as mem region 'idx', supporting
 * VA at from idx->guest_phys_addr + va_offset.
 *
 * Return 0 for success and -errno for errors
 */
static int km_alloc_region(int idx, size_t size, int upper_va)
{
   void* ptr;
   kvm_mem_reg_t* reg = &machine.vm_mem_regs[idx];
   km_gva_t base = memreg_base(idx);

   assert(reg->memory_size == 0 && reg->userspace_addr == 0);
   assert(machine.pdpe1g || (base < GIB || base >= machine.guest_max_physmem - GIB));
   if ((ptr = km_guest_page_malloc(base, size, PROT_NONE)) == NULL) {
      return -ENOMEM;
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = idx;
   reg->guest_phys_addr = base;
   reg->memory_size = size;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      warn("KVM: failed to plug memory region %d", idx);
      km_guest_page_free(base, size);
      memset(reg, 0, sizeof(*reg));
      return -errno;
   }
   set_pml4_hierarchy(reg, upper_va);
   return 0;
}

static void km_free_region(int idx, int upper_va)
{
   kvm_mem_reg_t* reg = &machine.vm_mem_regs[idx];
   uint64_t size = reg->memory_size;

   assert(size != 0);
   clear_pml4_hierarchy(reg, upper_va);
   reg->memory_size = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: failed to unplug memory region %d", idx);
   }
   km_guest_page_free(reg->guest_phys_addr, size);
   reg->userspace_addr = 0;
   reg->guest_phys_addr = 0;
}

static inline int km_region_allocated(int idx)
{
   kvm_mem_reg_t* reg = &machine.vm_mem_regs[idx];
   return (reg->userspace_addr != 0);
}

/*
 * brk() call implementation.
 *
 * Move machine.brk up or down, adding or removing memory regions as required.
 */
km_gva_t km_mem_brk(km_gva_t brk)
{
   int idx;
   int error = 0;

   if (brk == 0) {
      return machine.brk;
   }
   if (brk > GUEST_MEM_START_VA + GUEST_MEM_ZONE_SIZE_VA) {
      return -ENOMEM;
   }
   km_mem_lock();

   // Keep brk and tbrk out of the same 1 GIB region.
   if (rounddown(gva_to_gpa(brk), PDPTE_REGION) >= rounddown(gva_to_gpa(machine.tbrk), PDPTE_REGION)) {
      km_mem_unlock();
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.brk - 1);
   for (km_gva_t m_brk = MIN(brk, memreg_top(idx)); m_brk < brk; m_brk = MIN(brk, memreg_top(idx))) {
      /* Move to the next memreg. Allocate if required. */
      idx++;
      if (km_region_allocated(idx)) {
         /*
          * the slot is already populated which means the tbrk side allocated it.
          * Still need to establish virtual to physical page mapping on this side.
          */
         set_pml4_hierarchy(&machine.vm_mem_regs[idx], 0);
      } else if (km_alloc_region(idx, memreg_size(idx), 0) != 0) {
         idx--;
         brk = machine.brk;
         error = ENOMEM;
         break;
      }
   }
   int tbrk_idx = gva_to_memreg_idx(machine.tbrk);
   for (; idx > gva_to_memreg_idx(brk - 1); idx--) {
      if (idx < tbrk_idx) {   // don't free shared slot.
         km_free_region(idx, 0);
      }
   }
   fixup_bottom_page_tables(machine.brk, brk);
   km_gva_t oldpage = roundup(machine.brk, KM_PAGE_SIZE);
   km_gva_t newpage = roundup(brk, KM_PAGE_SIZE);
   if (oldpage < newpage) {
      mprotect(km_gva_to_kma_nocheck(oldpage), newpage - oldpage, PROT_READ | PROT_WRITE);
   } else if (newpage < oldpage) {
      mprotect(km_gva_to_kma_nocheck(newpage), oldpage - newpage, PROT_NONE);
   }
   machine.brk = brk;
   km_mem_unlock();
   return error == 0 ? brk : -error;
}

/*
 * Extends tbrk down in the upper virtual address (VA) space, mirroring behavior of km_mem_brk which
 * extends allocated space up in the bottom VA.
 * Note that gva in the upper VA are shifted up GUEST_VA_OFFSET from related gpa.
 */
km_gva_t km_mem_tbrk(km_gva_t tbrk)
{
   int idx;
   int error = 0;

   if (tbrk == 0) {
      return machine.tbrk;
   }
   if (GUEST_MEM_TOP_VA < tbrk || tbrk < GUEST_MEM_TOP_VA - GUEST_MEM_ZONE_SIZE_VA) {
      return -ENOMEM;
   }
   km_mem_lock();
   // Keep brk and tbrk out if the same 1 GIB region.
   km_gva_t brk_pa = gva_to_gpa(machine.brk);
   km_gva_t tbrk_pa = gva_to_gpa(tbrk);
   if (rounddown(brk_pa, PDPTE_REGION) >= rounddown(tbrk_pa, PDPTE_REGION)) {
      km_mem_unlock();
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.tbrk);
   for (km_gva_t m_brk = MAX(tbrk, gpa_to_upper_gva(memreg_base(idx))); m_brk > tbrk;
        m_brk = MAX(tbrk, gpa_to_upper_gva(memreg_base(idx)))) {
      /* Move to the next memreg. Allocate if required. */
      idx--;
      if (km_region_allocated(idx)) {
         /*
          * the slot is already populated which means the brk side allocated it.
          * Still need to establish virtual to physical page mapping on this side.
          */
         set_pml4_hierarchy(&machine.vm_mem_regs[idx], 1);
      } else if (km_alloc_region(idx, memreg_size(idx), 1) != 0) {
         idx++;
         tbrk = machine.tbrk;
         error = ENOMEM;
         break;
      }
   }
   int brk_idx = gva_to_memreg_idx(machine.brk - 1);
   for (; idx < gva_to_memreg_idx(tbrk); idx++) {
      if (idx > brk_idx) {   // don't free shared slot.
         /* brk moved down and left one or more memory regions. Remove and free */
         km_free_region(idx, 1);
      }
   }
   fixup_top_page_tables(machine.tbrk, tbrk);   // fix in-guest page tables used/unused flag
   // Note: below-tbrk mprotect is managed in guest mmaps (km_mmap.c), so here we just move tbrk up/down.
   machine.tbrk = tbrk;
   km_mem_unlock();
   return error == 0 ? tbrk : -error;
}
