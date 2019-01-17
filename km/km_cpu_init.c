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
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <assert.h>
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
 * - User space, starting with 2MB, each next increasing by two:
 *   2MB, 4MB, 8MB ... 512MB, 1GB, ... 256GB.
 * - hole from the end of user space, to 511GB
 * - guest stack is 2MB with the top at 512GB
 *
 * TODO: We are forced to stay within 512GB of guest physical memory
 * as KVM refuses to work if we try to use phys addr more than 512GB,
 * hence the GUEST_MEM_MAX value below
 *
 * In the absence of this limitation we'd put the stack up high
 */
static const int RSV_MEM_START = PAGE_SIZE;
static const int RSV_MEM_SIZE = PAGE_SIZE * 63;
/*
 * Mandatory data structures in reserved area to enable 64 bit CPU.
 * These numbers are offsets from the start of reserved area
 */
static const int RSV_GDT_OFFSET = 2 * PAGE_SIZE;
static const int RSV_IDT_OFFSET = 3 * PAGE_SIZE;
static const int RSV_PML4_OFFSET = 4 * PAGE_SIZE;
static const int RSV_PDPT_OFFSET = 5 * PAGE_SIZE;
static const int RSV_PDPT2_OFFSET = 6 * PAGE_SIZE;
static const int RSV_PD_OFFSET = 7 * PAGE_SIZE;
static const int RSV_PD2_OFFSET = 8 * PAGE_SIZE;
/*
 * convert the above to guest physical offsets
 */
#define RSV_GUEST_PA(x) ((x) + RSV_MEM_START)

#define GUEST_MEM_START_PA memreg_base(KM_TEXT_DATA_MEMSLOT)
static const uint64_t GUEST_MEM_MAX = 510 * GIB;

/*
 * Segment constants. They go directly to ioctl to set x86 registers, and
 * translate to GDT entries in memory
 */
static const kvm_seg_t seg_code = {
    .selector = 1,
    .limit = 0xffffffff,
    .type = 9, /* Execute-only, accessed */
    .present = 1,
    .s = 1,
    .l = 1,
    .g = 1,
};

static const kvm_seg_t seg_data = {
    .selector = 2,
    .limit = 0xffffffff,
    .type = 3, /* Read-write, accessed */
    .present = 1,
    .db = 1,
    .s = 1,
    .g = 1,
};

static const kvm_seg_t seg_tr = {
    .base = 0,
    .limit = 0,
    .type = 11, /* 64-bit TSS, busy */
    .present = 1,
};

static const kvm_seg_t seg_unused = {
    .unusable = 1,
};

/*
 * GDT in the first reserved page, idt in the second
 *
 * TODO: Do we even need to specify idt if there are no interrupts?
 */
static void km_init_gdt_idt(void *mem)
{
   x86_seg_d_t *seg = mem + RSV_GDT_OFFSET;
   /*
    * Three first slots in gdt - none, code, data
    */
   static const x86_seg_d_t gdt[] = {
       {0},
       {.limit_hi = 0xf,
        .limit_lo = 0xffff,
        .type = 9,
        .p = 1,
        .s = 1,
        .l = 1,
        .g = 1},
       {.limit_hi = 0xf,
        .limit_lo = 0xffff,
        .type = 3,
        .p = 1,
        .d_b = 1,
        .s = 1,
        .g = 1},
   };

   memcpy(seg, gdt, 3 * sizeof(*seg));

   /* IDT - just zeroed out, or do we need to ``.type = 0b1110;''? */
   seg = mem + RSV_IDT_OFFSET;
   memset(seg, 0, PAGE_SIZE);
}

static void pml4e_set(x86_pml4e_t *pml4e, uint64_t pdpt)
{
   pml4e->p = 1;
   pml4e->r_w = 1;
   pml4e->u_s = 1;
   pml4e->pdpt = pdpt >> 12;
}

static void pdpte_set(x86_pdpte_t *pdpe, uint64_t pd)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->pd = pd >> 12;
}

static void pdpte_1g_set(x86_pdpte_1g_t *pdpe, uint64_t addr)
{
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->u_s = 1;
   pdpe->ps = 1;
   pdpe->page = addr >> 30;
}

static void pde_2mb_set(x86_pde_2m_t *pde, u_int64_t addr)
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
 * exponentially increasing areas, staring with 2MB to the total of
 * GUEST_MEM_MAX. Note virtual and physical layout are equivalent.
 *
 * Guest stack starting GUEST_STACK_START_VA, GUEST_STACK_START_SIZE. We chose,
 * somewhat randonly, GUEST_STACK_START_VA at the last GB of the first half of
 * 48 bit wide virtual address space. See km.h for values.
 *
 * We use 2MB pages for the first GB, and 1GB pages for the rest. With the
 * GUEST_MEM_MAX of 510GB we need two pml4 entries, #0 and #255, #0 covers the
 * text and data, the #255 stack. There are correspondingly two pdpt pages. We
 * use only the very last entry in the second one. Initially only the second
 * entry of the first one is used, but then it expands with brk. If we ever go
 * over the 512GB limit there will be a need for more pdpt pages. The first
 * entry points to the pd page that covers the first GB.
 *
 * TODO: Check for hugepage support
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
static void init_pml4(void *mem)
{
   x86_pml4e_t *pml4e;
   x86_pdpte_t *pdpe;
   x86_pde_2m_t *pde;
   int idx;

   pml4e = mem + RSV_PML4_OFFSET;
   memset(pml4e, 0, PAGE_SIZE);
   pml4e_set(pml4e, RSV_GUEST_PA(RSV_PDPT_OFFSET));       // entry #0

   pdpe = mem + RSV_PDPT_OFFSET;
   memset(pdpe, 0, PAGE_SIZE);
   pdpte_set(pdpe, RSV_GUEST_PA(RSV_PD_OFFSET));       // first entry for the first GB

   pde = mem + RSV_PD_OFFSET;
   memset(pde, 0, PAGE_SIZE);
   pde_2mb_set(pde + 1, GUEST_MEM_START_PA);       // second entry for the 2MB - 4MB 

   idx = GUEST_STACK_START_VA / (512 * GIB);
   assert(idx < 512);

   // check if we need the second pml4 entry, i.e the two mem regions are more
   // than 512 GB apart. If we do, make the second entry to the second pdpt page
   if (idx > 0) {
      // prepare the second pdpt page if needed
      pdpe = mem + RSV_PDPT2_OFFSET;
      pml4e_set(pml4e + idx, RSV_GUEST_PA(RSV_PDPT2_OFFSET));
      memset(pdpe, 0, PAGE_SIZE);
   }
   idx = (GUEST_STACK_START_VA / GIB) & 0x1ff;
   pdpte_set(pdpe + idx, RSV_GUEST_PA(RSV_PD2_OFFSET));

   idx = (GUEST_STACK_START_VA / (2 * MIB)) & 0x1ff;
   pde = mem + RSV_PD2_OFFSET;
   memset(pde, 0, PAGE_SIZE);
   pde_2mb_set(pde + idx, GUEST_STACK_START_PA);   
}

static void *page_malloc(size_t size)
{
   void *addr;

   if ((size & (PAGE_SIZE - 1)) != 0) {
      errno = EINVAL;
      return NULL;
   }
   if ((addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
      return NULL;
   }
   return addr;
}

static void page_free(void *addr, size_t size)
{
   munmap(addr, size);
}

/*
 * Initialize GDT, IDT, and PML4 structures in the reserved region
 * PML4 doesn't map the reserved region, it becomes hidden from the guest
 */
static void km_mem_init(void)
{
   kvm_mem_reg_t *reg;
   void *ptr;

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
   reg->flags = 0;       // set to KVM_MEM_READONLY for readonly
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set reserved region failed");
   }
   machine.vm_mem_regs[KM_RSRV_MEMSLOT] = reg;
   km_init_gdt_idt((void *)reg->userspace_addr);
   init_pml4((void *)reg->userspace_addr);

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
static void set_pml4_hierarchy(kvm_mem_reg_t *reg)
{
   int idx = reg->slot;
   uint64_t size = reg->memory_size;
   uint64_t base = memreg_base(idx);

   if (size < GIB) {
      x86_pde_2m_t *pde = (x86_pde_2m_t *)(machine.vm_mem_regs[KM_RSRV_MEMSLOT]
                                               ->userspace_addr +
                                           RSV_PD_OFFSET);
      for (uint64_t i = 0; i < size; i += 2 * MIB) {
         pde_2mb_set(pde + (i + base)/ (2 * MIB), reg->guest_phys_addr + i);
      }
   } else {
      x86_pdpte_1g_t *pdpe =
          (x86_pdpte_1g_t *)(machine.vm_mem_regs[KM_RSRV_MEMSLOT]
                                 ->userspace_addr +
                             RSV_PDPT_OFFSET);
      for (uint64_t i = 0; i < size; i += GIB) {
         pdpte_1g_set(pdpe + (i + base)/ GIB, reg->guest_phys_addr + i);
      }
   }
}

static void clear_pml4_hierarchy(kvm_mem_reg_t *reg)
{
   int idx = reg->slot;
   uint64_t size = reg->memory_size;
   uint64_t base = memreg_base(idx);

   if (size < GIB) {
      x86_pde_2m_t *pde = (x86_pde_2m_t *)(machine.vm_mem_regs[KM_RSRV_MEMSLOT]
                                               ->userspace_addr +
                                           RSV_PD_OFFSET);
      for (uint64_t i = 0; i < size; i += 2 * MIB) {
         memset(pde + (i + base)/ (2 * MIB), 0, sizeof(*pde));
      }
   } else {
      x86_pdpte_1g_t *pdpe =
          (x86_pdpte_1g_t *)(machine.vm_mem_regs[KM_RSRV_MEMSLOT]
                                 ->userspace_addr +
                             RSV_PDPT_OFFSET);
      for (uint64_t i = 0; i < size; i += GIB) {
         memset(pdpe + (i + base)/ GIB, 0, sizeof(*pdpe));
      }
   }
}

/*
 * brk() call implementation.
 *
 * Move machine.brk up or down, adding or removing memory regions as required.
 */
uint64_t km_mem_brk(uint64_t brk)
{
   int idx;
   uint64_t size, m_brk;
   void *ptr;
   kvm_mem_reg_t *reg;

   if (brk == 0 || brk == machine.brk) {
      return machine.brk;
   }
   if (brk > GUEST_MEM_MAX) {
      return -EINVAL;
   }

   idx = gva_to_memreg_idx(machine.brk);
   m_brk = MIN(brk, memreg_base(idx) + memreg_size(idx));
   for (; m_brk < brk; m_brk = MIN(brk, memreg_base(idx) + memreg_size(idx))) {
      /* Not enough room in the allocated memreg, allocate and add new ones */
      idx++;
      if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
         return -ENOMEM;
      }
      size = memreg_size(idx);
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
      page_free((void *)reg->userspace_addr, size);
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
      km_vcpu_t *vcpu = machine.vm_vcpus[machine.vm_cpu_cnt - 1];

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
      kvm_mem_reg_t *mr = machine.vm_mem_regs[i];

      if (mr != NULL) {
         /* TODO: Do we need to "unplug" it from the VM? */
         if (mr->memory_size != 0) {
            page_free((void *)mr->userspace_addr, mr->memory_size);
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

static void kvm_vcpu_init_sregs(int fd)
{
   static const kvm_sregs_t sregs = {
       .cr0 = X86_CR0_PE | X86_CR0_PG | X86_CR0_WP | X86_CR0_NE,
       .cr3 = RSV_MEM_START + RSV_PML4_OFFSET,
       .cr4 = X86_CR4_PSE | X86_CR4_PAE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT,
       .efer = X86_EFER_LME | X86_EFER_LMA,

       .cs = seg_code,
       .ss = seg_data,
       .ds = seg_data,
       .es = seg_data,
       .fs = seg_data,
       .gs = seg_data,

       .gdt = {.base = RSV_MEM_START + RSV_GDT_OFFSET, .limit = PAGE_SIZE},
       .idt = {.base = RSV_MEM_START + RSV_IDT_OFFSET, .limit = PAGE_SIZE},
       .tr = seg_tr,
       .ldt = seg_unused,
   };

   if (ioctl(fd, KVM_SET_SREGS, &sregs) < 0) {
      err(1, "KVM: set sregs failed");
   }
}

/*
 * Create vcpu, map the control region, initialize sregs.
 * Set RIP, SP, RFLAGS, clear the rest of the regs.
 * vpcu is ready to run starting with instruction @RIP
 */
km_vcpu_t *km_vcpu_init(uint64_t ent, uint64_t sp)
{
   km_vcpu_t *vcpu;
   int cnt = machine.vm_cpu_cnt;

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
   if ((vcpu->kvm_vcpu_fd = ioctl(machine.mach_fd, KVM_CREATE_VCPU, cnt)) < 0) {
      err(1, "KVM: create vcpu %d failed", cnt);
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_CPUID2, machine.cpuid) < 0) {
      err(1, "KVM: set CPUID2 failed");
   }
   if ((vcpu->cpu_run = mmap(NULL, machine.vm_run_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, vcpu->kvm_vcpu_fd, 0)) == MAP_FAILED) {
      err(1, "KVM: failed mmap VCPU %d control region", cnt);
   }
   machine.vm_vcpus[cnt++] = vcpu;
   kvm_vcpu_init_sregs(vcpu->kvm_vcpu_fd);

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
   if ((machine.vm_run_size =
            ioctl(machine.kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0)) < 0) {
      err(1, "KVM: get VM memory region size failed");
   }
   if (machine.vm_run_size < sizeof(kvm_run_t)) {
      errx(1,
           "KVM: suspicious VM memory region size %zu, expecting at least %zu",
           machine.vm_run_size, sizeof(kvm_run_t));
   }
   if ((machine.cpuid =
            malloc(sizeof(kvm_cpuid2_t) +
                   CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2))) == NULL) {
      err(1, "KVM: no memory for CPUID");
   }
   machine.cpuid->nent = CPUID_ENTRIES;
   if (ioctl(machine.kvm_fd, KVM_GET_SUPPORTED_CPUID, machine.cpuid) < 0) {
      err(1, "KVM: get supported CPUID failed");
   }
   km_mem_init();
}
