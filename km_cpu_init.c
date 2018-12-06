/*
 * TODO: Header
 */

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "km.h"
#include "x86_cpu.h"

/*
 * kernel include/linux/kvm_host.h
 */
static const int CPUID_ENTRIES = 100;       // A little padding, kernel says 80
#define KVM_USER_MEM_SLOTS 512
#define KVM_MAX_VCPUS 288

typedef struct km_machine {
   int kvm_fd;                               // /dev/kvm file desciprtor
   int mach_fd;                              // VM file desciprtor
   size_t vm_run_size;                       // size of the run control region
                                             //
   int vm_cpu_cnt;                           // count of the below
   km_vcpu_t *vm_vcpus[KVM_MAX_VCPUS];       // VCPUs we created
   int vm_mem_reg_cnt;                       // count of the below
   kvm_mem_reg_t
       *vm_mem_regs[KVM_USER_MEM_SLOTS];       // guest physical memory regions
                                               //
   kvm_cpuid2_t *cpuid;                        // to set VCPUs cpuid
} km_machine_t;

static km_machine_t machine = {
    .kvm_fd = -1,
    .mach_fd = -1,
};

/*
 * Physical memory layout:
 * - 4k hole
 * - 63 pages reserved area, used for pml4, gdt, idt
 * - hole till 2MB
 * - 1GB user space
 *
 * TODO: make layout dynamic, per command line
 * TODO: equivalent of brk()/sbrk() system call could be used for guest to
 * request more memory
 */

typedef enum {
   KM_MEMSLOT_BASE = 0,
   KM_RSRV_MEMSLOT = KM_MEMSLOT_BASE,
   KM_GUEST_MEMSLOT,
   KM_MEMSLOT_CNT
} km_memslot_t;

static const int GUEST_MEM_START = PAGE_SIZE * 512;                    // 2MB
static const uint64_t GUEST_MEM_SIZE = PAGE_SIZE * 1024 * 256ul;       // 1GB

static const int RSV_MEM_START = PAGE_SIZE;
static const int RSV_MEM_SIZE = PAGE_SIZE * 63;

/*
 * Mandatory data structures in reserved memory to enable 64 bit CPU
 */
static const int RSV_GDT_OFFSET = 2 * PAGE_SIZE;
static const int RSV_IDT_OFFSET = 3 * PAGE_SIZE;
static const int RSV_PML4_OFFSET = 4 * PAGE_SIZE;
static const int RSV_PDPT_OFFSET = 5 * PAGE_SIZE;
static const int RSV_PDT_OFFSET = 6 * PAGE_SIZE;

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

static const kvm_seg_t seg_unus = {
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

/*
 * Setup pagetables in the guest. We use 2MB pages for now.
 * We need only one pml4 emtry, one pdtd entry, and one pdt page (512 entries)
 * per 1GB of address space.
 *
 * TODO: Check for hugepage support
 */
static void km_init_pml4(void *mem)
{
   x86_pml4e_t *pml4e = mem + RSV_PML4_OFFSET;
   x86_pdpte_t *pdpe = mem + RSV_PDPT_OFFSET;
   x86_pde_2m_t *pde = mem + RSV_PDT_OFFSET;
   uint64_t pa;

   memset(pml4e, 0, PAGE_SIZE);
   pml4e->p = 1;
   pml4e->r_w = 1;
   pml4e->accessed = 1;
   //    pml4e->u_s = 1,
   pml4e->pdpt = (RSV_PDPT_OFFSET + RSV_MEM_START) >> 12;

   memset(pdpe, 0, PAGE_SIZE);
   pdpe->p = 1;
   pdpe->r_w = 1;
   pdpe->accessed = 1;
   //    pdpe->u_s = 1;
   pdpe->pd = (RSV_PDT_OFFSET + RSV_MEM_START) >> 12;

   memset(pde, 0, PAGE_SIZE);
   for (pa = GUEST_MEM_START; pa < GUEST_MEM_START + GUEST_MEM_SIZE;
        pa += HUGE_PAGE_SIZE, pde++) {
      pde->p = 1;
      pde->r_w = 1;
      pde->accessed = 1;
      pde->ps = 1;
      pde->page = pa >> 21;
   }
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
 * Guest memory
 *
 * We set up two regions:
 * 1. Small (RSV_MEM_SIZE) readonly region with gdt, pml4, and idt
 * 2. Main part used by payload, starting at 2MB
 *
 * Initialize GDT, IDT, and PML4 structures in the first region
 * PML4 doesn't map the reserved region, it becomes hidden from the guest
 */

/*
 * Knowing memory layout and how pml4 is set,
 * convert between guest virtual address and km address
 */
void *km_gva_to_kma(uint64_t ga)
{
   if (ga >= GUEST_MEM_SIZE) {
      errx(1, "km_gva_to_kma: bad guest address 0x%lx", ga);
   }
   return (void *)(machine.vm_mem_regs[KM_GUEST_MEMSLOT]->userspace_addr + ga);
}

uint64_t km_kma_to_gva(void *ka)
{
   uint64_t addr = (typeof(addr))ka;

   if (addr < machine.vm_mem_regs[KM_GUEST_MEMSLOT]->userspace_addr ||
       addr >= machine.vm_mem_regs[KM_GUEST_MEMSLOT]->memory_size) {
      errx(1, "km_kma_to_gva: bad km address %p", ka);
   }
   return addr - machine.vm_mem_regs[KM_GUEST_MEMSLOT]->userspace_addr;
}

uint64_t km_guest_memsize(void)
{
   return machine.vm_mem_regs[KM_GUEST_MEMSLOT]->memory_size;
}

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
   reg->flags = 0;       // KVM_MEM_READONLY;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set reserved region failed");
   }
   machine.vm_mem_regs[KM_RSRV_MEMSLOT] = reg;
   km_init_gdt_idt((void *)reg->userspace_addr);
   km_init_pml4((void *)reg->userspace_addr);

   /* 2. Payload memory */
   if ((reg = malloc(sizeof(kvm_mem_reg_t))) == NULL) {
      err(1, "KVM: no memory for mem region");
   }
   if ((ptr = page_malloc(GUEST_MEM_SIZE)) == NULL) {
      err(1, "KVM: no memory for guest payload");
   }
   reg->userspace_addr = (typeof(reg->userspace_addr))ptr;
   reg->slot = KM_GUEST_MEMSLOT;
   reg->guest_phys_addr = GUEST_MEM_START;
   reg->memory_size = GUEST_MEM_SIZE;
   reg->flags = 0;
   if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, reg) < 0) {
      err(1, "KVM: set guest payload memory failed");
   }
   machine.vm_mem_regs[KM_GUEST_MEMSLOT] = reg;
   machine.vm_mem_reg_cnt = KM_MEMSLOT_CNT;
   //    km_init_gdt_idt((void *)reg->userspace_addr);
   //    km_init_pml4((void *)reg->userspace_addr);
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
   for (; machine.vm_mem_regs; machine.vm_mem_reg_cnt--) {
      kvm_mem_reg_t *mr = machine.vm_mem_regs[machine.vm_mem_reg_cnt - 1];

      /* TODO: Do we need to "unplug" it from the VM? */
      page_free((void *)mr->userspace_addr, mr->memory_size);
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
       .ldt = seg_unus,
   };

   if (ioctl(fd, KVM_SET_SREGS, &sregs) < 0) {
      err(1, "KVM: set sregs failed");
   }
}

/*
 * Creat vcpu, map the control region, initialize sregs.
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
