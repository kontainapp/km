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
#include "x86_cpu.h"

km_machine_t machine = {
    .kvm_fd = -1,
    .mach_fd = -1,
};

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

#define GUEST_MEM_START_PA memreg_base(KM_TEXT_DATA_MEMSLOT)

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
 * Guest stack starting GUEST_STACK_START_VA, GUEST_STACK_START_SIZE. We chose,
 * somewhat randonly, GUEST_STACK_START_VA at the last 2MB of the first half of
 * 48 bit wide virtual address space. See km.h for values.
 *
 * We use 2MB pages for the first GB, and 1GB pages for the rest. With the
 * guest_max_physmem <= 512GB we need two pml4 entries, #0 and #255, #0 covers
 * the text and data, the #255 stack. There are correspondingly two pdpt pages.
 * We use only the very last entry in the second one. Initially only the second
 * entry of the first one is used, but then it expands with brk. If we ever go
 * over the 512GB limit there will be a need for more pdpt pages. The first
 * entry points to the pd page that covers the first GB.
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
   x86_pde_2m_t* pde;
   int idx;

   assert(machine.guest_max_physmem <= PML4E_REGION);
   pml4e = mem + RSV_PML4_OFFSET;
   memset(pml4e, 0, PAGE_SIZE);
   pml4e_set(pml4e, RSV_GUEST_PA(RSV_PDPT_OFFSET));   // entry #0

   pdpe = mem + RSV_PDPT_OFFSET;
   memset(pdpe, 0, PAGE_SIZE);
   pdpte_set(pdpe, RSV_GUEST_PA(RSV_PD_OFFSET));   // first entry for the first GB

   pde = mem + RSV_PD_OFFSET;
   memset(pde, 0, PAGE_SIZE);
   pde_2mb_set(pde + 1, GUEST_MEM_START_PA);   // second entry for the 2MB - 4MB

   idx = GUEST_STACK_START_VA / PML4E_REGION;
   assert(idx < PAGE_SIZE / sizeof(x86_pml4e_t));   // within pml4 page

   // check if we need the second pml4 entry, i.e the two mem regions are more
   // than 512 GB apart. If we do, make the second entry to the second pdpt page
   if (idx > 0) {
      // prepare the second pdpt page if needed
      pdpe = mem + RSV_PDPT2_OFFSET;
      pml4e_set(pml4e + idx, RSV_GUEST_PA(RSV_PDPT2_OFFSET));
      memset(pdpe, 0, PAGE_SIZE);
   }
   idx = (GUEST_STACK_START_VA / PDPTE_REGION) & 0x1ff;
   pdpte_set(pdpe + idx, RSV_GUEST_PA(RSV_PD2_OFFSET));

   idx = (GUEST_STACK_START_VA / PDE_REGION) & 0x1ff;
   pde = mem + RSV_PD2_OFFSET;
   memset(pde, 0, PAGE_SIZE);
   pde_2mb_set(pde + idx, GUEST_STACK_START_PA);
}

static km_kma_t page_malloc(size_t size)
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

static void page_free(km_kma_t addr, size_t size)
{
   munmap(addr, size);
}

/*
 * Initialize GDT, IDT, and PML4 structures in the reserved region
 * PML4 doesn't map the reserved region, it becomes hidden from the guest
 */
static void km_mem_init(void)
{
   kvm_mem_reg_t* reg;
   km_kma_t ptr;

   /* 1. reserved memory */
   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      err(1, "KVM: no memory for mem region");
   }
   if ((ptr = page_malloc(RSV_MEM_SIZE)) == NULL) {
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

   /* 2. data and text memory, account for brk */
   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      err(1, "KVM: no memory for mem region");
   }
   if ((ptr = page_malloc(memreg_size(KM_TEXT_DATA_MEMSLOT))) == NULL) {
      err(1, "KVM: no memory for guest payload");
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = KM_TEXT_DATA_MEMSLOT;
   reg->guest_phys_addr = GUEST_MEM_START_PA;
   reg->memory_size = memreg_size(KM_TEXT_DATA_MEMSLOT);
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set guest memory failed");
   }
   machine.vm_mem_regs[KM_TEXT_DATA_MEMSLOT] = reg;
   machine.brk = GUEST_MEM_START_VA + PAGE_SIZE;

   /* 3. Stack */
   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      err(1, "KVM: no memory for stack mem region");
   }
   if ((ptr = page_malloc(GUEST_STACK_START_SIZE)) == NULL) {
      err(1, "KVM: no memory for guest payload stack");
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = KM_STACK_MEMSLOT;
   reg->guest_phys_addr = GUEST_STACK_START_PA;
   reg->memory_size = GUEST_STACK_START_SIZE;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set guest payload stack memory failed");
   }
   machine.vm_mem_regs[KM_STACK_MEMSLOT] = reg;
}

/*
 * Set and clear pml4 hierarchy entries to reflect addition or removal of reg,
 * following the setup in init_pml4(). Set/Clears pdpte or pde depending on the
 * size.
 */
static void set_pml4_hierarchy(kvm_mem_reg_t* reg)
{
   int idx = reg->slot;
   uint64_t size = reg->memory_size;
   uint64_t base = memreg_base(idx);

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_memreg_kma(KM_RSRV_MEMSLOT) + RSV_PD_OFFSET;
      for (uint64_t i = 0; i < size; i += PDE_REGION) {
         pde_2mb_set(pde + (i + base) / PDE_REGION, reg->guest_phys_addr + i);
      }
   } else {
      assert(machine.pdpe1g != 0);   // no 1GB pages support
      x86_pdpte_1g_t* pdpe = km_memreg_kma(KM_RSRV_MEMSLOT) + RSV_PDPT_OFFSET;
      for (uint64_t i = 0; i < size; i += PDPTE_REGION) {
         pdpte_1g_set(pdpe + (i + base) / PDPTE_REGION, reg->guest_phys_addr + i);
      }
   }
}

static void clear_pml4_hierarchy(kvm_mem_reg_t* reg)
{
   int idx = reg->slot;
   uint64_t size = reg->memory_size;
   uint64_t base = memreg_base(idx);

   if (size < PDPTE_REGION) {
      x86_pde_2m_t* pde = km_memreg_kma(KM_RSRV_MEMSLOT) + RSV_PD_OFFSET;
      for (uint64_t i = 0; i < size; i += PDE_REGION) {
         memset(pde + (i + base) / PDE_REGION, 0, sizeof(*pde));
      }
   } else {
      assert(machine.pdpe1g != 0);   // no 1GB pages support
      x86_pdpte_1g_t* pdpe = km_memreg_kma(KM_RSRV_MEMSLOT) + RSV_PDPT_OFFSET;
      for (uint64_t i = 0; i < size; i += PDPTE_REGION) {
         memset(pdpe + (i + base) / PDPTE_REGION, 0, sizeof(*pdpe));
      }
   }
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
   km_kma_t ptr;
   kvm_mem_reg_t* reg;

   if (brk == 0 || brk == machine.brk) {
      return machine.brk;
   }
   if (brk > GUEST_MAX_BRK) {
      return -ENOMEM;
   }

   idx = gva_to_memreg_idx(machine.brk);
   m_brk = MIN(brk, memreg_top(idx));
   for (; m_brk < brk; m_brk = MIN(brk, memreg_top(idx))) {
      /* Not enough room in the allocated memreg, allocate and add new ones */
      idx++;
      if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
         return -ENOMEM;
      }
      size = memreg_top(idx) < GUEST_MAX_BRK ? memreg_size(idx) : GUEST_MAX_BRK - memreg_base(idx);
      if ((ptr = page_malloc(size)) == NULL) {
         free(reg);
         return -ENOMEM;
      }
      reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
      reg->slot = idx;
      reg->guest_phys_addr = memreg_base(idx);
      reg->memory_size = size;
      reg->flags = 0;
      if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
         page_free(ptr, size);
         free(reg);
         return -errno;
      }
      machine.vm_mem_regs[idx] = reg;
      set_pml4_hierarchy(reg);
   }
   for (; idx > gva_to_memreg_idx(brk); idx--) {
      /* brk moved down and left one or more memory regions. Remove and free */
      reg = machine.vm_mem_regs[idx];
      size = reg->memory_size;
      clear_pml4_hierarchy(reg);
      reg->memory_size = 0;
      if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
         err(1, "KVM: failed to unplug memory region %d", idx);
      }
      page_free((km_kma_t)reg->userspace_addr, size);
      free(reg);
      machine.vm_mem_regs[idx] = NULL;
   }
   machine.brk = brk;
   return machine.brk;
}

/*
 * TODO: Is it needed as we exiting anyways?
 */
void km_machine_fini(void)
{
   /* check if there are any VCPUs */
   for (; machine.vm_cpu_cnt; machine.vm_cpu_cnt--) {
      km_vcpu_t* vcpu = machine.vm_vcpus[machine.vm_cpu_cnt - 1];

      if (vcpu != NULL) {
         if (vcpu->cpu_run != NULL) {
            (void)munmap(vcpu->cpu_run, machine.vm_run_size);
         }
         if (vcpu->kvm_vcpu_fd >= 0) {
            close(vcpu->kvm_vcpu_fd);
         }
         free(vcpu);
      }
   }
   /* check if there are any memory regions */
   for (int i = KVM_USER_MEM_SLOTS - 1; i >= 0; i--) {
      kvm_mem_reg_t* mr = machine.vm_mem_regs[i];

      if (mr != NULL) {
         /* TODO: Do we need to "unplug" it from the VM? */
         if (mr->memory_size != 0) {
            page_free((km_kma_t)mr->userspace_addr, mr->memory_size);
         }
         free(mr);
         machine.vm_mem_regs[i] = NULL;
      }
   }
   /* Now undo things done in km_machine_init */
   if (machine.cpuid != NULL) {
      free(machine.cpuid);
      machine.cpuid = NULL;
   }
   if (machine.mach_fd >= 0) {
      close(machine.mach_fd);
      machine.mach_fd = -1;
   }
   if (machine.kvm_fd >= 0) {
      close(machine.kvm_fd);
      machine.kvm_fd = -1;
   }
   km_hcalls_fini();
}

static void kvm_vcpu_init_sregs(int fd, uint64_t fs)
{
   static const kvm_sregs_t sregs_template = {
       .cr0 = X86_CR0_PE | X86_CR0_PG | X86_CR0_WP | X86_CR0_NE,
       .cr3 = RSV_MEM_START + RSV_PML4_OFFSET,
       .cr4 = X86_CR4_PSE | X86_CR4_PAE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT,
       .efer = X86_EFER_LME | X86_EFER_LMA,

       .cs = {.limit = 0xffffffff,
              .type = 9, /* Execute-only, accessed */
              .present = 1,
              .s = 1,
              .l = 1,
              .g = 1},
       .tr = {.type = 11, /* 64-bit TSS, busy */
              .present = 1},
   };
   kvm_sregs_t sregs = sregs_template;

   sregs.fs.base = fs;
   if (ioctl(fd, KVM_SET_SREGS, &sregs) < 0) {
      err(1, "KVM: set sregs failed");
   }
}

/*
 * Create vcpu, map the control region, initialize sregs.
 * Set RIP, SP, RFLAGS, clear the rest of the regs.
 * VCPU is ready to run starting with instruction @RIP
 */
km_vcpu_t* km_vcpu_init(km_gva_t ent, km_gva_t sp, uint64_t fs_base)
{
   km_vcpu_t* vcpu;

   /*
    * TODO: consider returning errors to be able to keep going instead of just
    * giving up
    */
   /*
    * TODO: check if we have too many VCPUs already. Need to get the
    * KVM_CAP_NR_VCPUS and/or KVM_CAP_NR_VCPUS in km_machine_init()
    */
   if ((vcpu = malloc(sizeof(km_vcpu_t))) == NULL) {
      err(1, "KVM: no memory for vcpu");
   }
   if ((vcpu->kvm_vcpu_fd = ioctl(machine.mach_fd, KVM_CREATE_VCPU, machine.vm_cpu_cnt)) < 0) {
      err(1, "KVM: create vcpu %d failed", machine.vm_cpu_cnt);
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_CPUID2, machine.cpuid) < 0) {
      err(1, "KVM: set CPUID2 failed");
   }
   if ((vcpu->cpu_run = mmap(
            NULL, machine.vm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->kvm_vcpu_fd, 0)) ==
       MAP_FAILED) {
      err(1, "KVM: failed mmap VCPU %d control region", machine.vm_cpu_cnt);
   }
   machine.vm_vcpus[machine.vm_cpu_cnt++] = vcpu;
   kvm_vcpu_init_sregs(vcpu->kvm_vcpu_fd, fs_base);

   /* per ABI, make sure sp + 8 is 16 aligned */
   sp &= ~(7ul);
   sp -= (sp + 8) % 16;

   kvm_regs_t regs = {
       .rip = ent,
       .rflags = X86_RFLAGS_FIXED,
       .rsp = sp,
   };
   if (ioctl(machine.vm_vcpus[0]->kvm_vcpu_fd, KVM_SET_REGS, &regs) < 0) {
      err(1, "KVM: set regs failed");
   }
   return vcpu;
}

/*
 * initial steps setting our VM
 *
 * talk to KVM
 * create VM
 * set VM run memory region
 * prepare cpuid
 *
 * Any failure is fatal, hence void
 */
void km_machine_init(void)
{
   int rc;

   if ((machine.kvm_fd = open("/dev/kvm", O_RDWR /* | O_CLOEXEC */)) < 0) {
      err(1, "KVM: Can't open /dev/kvm");
   }
   if ((rc = ioctl(machine.kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
      err(1, "KVM: get API version failed");
   }
   if (rc != KVM_API_VERSION) {
      errx(1, "KVM: API version mismatch");
   }
   if ((machine.mach_fd = ioctl(machine.kvm_fd, KVM_CREATE_VM, NULL)) < 0) {
      err(1, "KVM: create VM failed");
   }
   if ((machine.vm_run_size = ioctl(machine.kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0)) < 0) {
      err(1, "KVM: get VM memory region size failed");
   }
   if (machine.vm_run_size < sizeof(kvm_run_t)) {
      errx(1,
           "KVM: suspicious VM memory region size %zu, expecting at least %zu",
           machine.vm_run_size,
           sizeof(kvm_run_t));
   }
   if ((machine.cpuid = malloc(sizeof(kvm_cpuid2_t) +
                               CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2))) == NULL) {
      err(1, "KVM: no memory for CPUID");
   }
   machine.cpuid->nent = CPUID_ENTRIES;
   if (ioctl(machine.kvm_fd, KVM_GET_SUPPORTED_CPUID, machine.cpuid) < 0) {
      err(1, "KVM: get supported CPUID failed");
   }
   /*
    * SDM, Table 3-8. Information Returned by CPUID.
    * Get and save max CPU supported phys memory.
    * Check for 1GB pages support
    */
   for (int i = 0; i < machine.cpuid->nent; i++) {
      switch (machine.cpuid->entries[i].function) {
         case 0x80000008:
            warnx("KVM: physical memory width %d", machine.cpuid->entries[i].eax & 0xff);
            machine.guest_max_physmem = 1ul << (machine.cpuid->entries[i].eax & 0xff);
            break;
         case 0x80000001:
            machine.pdpe1g = ((machine.cpuid->entries[i].edx & 1ul << 26) != 0);
            break;
      }
   }
   if (machine.pdpe1g == 0) {
      /*
       * In the absence of 1gb pages we can only support 2GB, first for
       * text+data, and the second for the stack. See assert() in
       * set_pml4_hierarchy()
       */
      machine.guest_max_physmem = MIN(2 * GIB, machine.guest_max_physmem);
   }
   km_mem_init();
}
