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

#include <stdint.h>
#include <linux/kvm.h>
#include <err.h>

static const uint64_t PAGE_SIZE = 0x1000;       // standard 4k page
static const uint64_t GIB = 0x40000000ul;       // GByte

/*
 * Types from linux/kvm.h
 */
typedef struct kvm_userspace_memory_region kvm_mem_reg_t;
typedef struct kvm_run kvm_run_t;
typedef struct kvm_cpuid2 kvm_cpuid2_t;
typedef struct kvm_segment kvm_seg_t;
typedef struct kvm_sregs kvm_sregs_t;
typedef struct kvm_regs kvm_regs_t;

typedef struct km_vcpu {
   int kvm_vcpu_fd;          // this VCPU file desciprtors
   kvm_run_t *cpu_run;       // run control region
} km_vcpu_t;

int load_elf(const char *file, void *mem, uint64_t *entry, uint64_t *end);
void km_machine_init(void);
km_vcpu_t *km_vcpu_init(uint64_t ent, uint64_t sp);
void km_vcpu_run(km_vcpu_t *vcpu);

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
#define KM_MAX_HCALL 512

typedef int (*km_hcall_fn_t)(int hc __attribute__((__unused__)),
                             uint64_t guest_addr, int *status);

extern km_hcall_fn_t km_hcalls_table[];

void km_hcalls_init(void);
void km_hcalls_fini(void);

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
   uint64_t brk;                               //
                                               //
   kvm_cpuid2_t *cpuid;                        // to set VCPUs cpuid
} km_machine_t;

typedef enum {
   KM_MEMSLOT_BASE = 0,
   KM_RSRV_MEMSLOT = KM_MEMSLOT_BASE,
   KM_TEXT_DATA_MEMSLOT,
   KM_STACK_MEMSLOT,
   KM_MEMSLOT_CNT
} km_memslot_t;

extern km_machine_t machine;

/*
 * See "Virtual memory layout:" in km_cpu_init.c for details.
 */
// Last GB of the first half of 2^48 virt address
static const uint64_t GUEST_STACK_TOP = 128 * 1024 * GIB - GIB;
static const uint64_t GUEST_STACK_START_SIZE = GIB;
static const uint64_t GUEST_STACK_START_VA =
    GUEST_STACK_TOP - GUEST_STACK_START_SIZE;       // 0x7fffc0000000
static const uint64_t GUEST_STACK_START_PA = 511 * GIB;

/*
 * Knowing memory layout and how pml4 is set,
 * convert between guest virtual address and km address
 */
static inline uint64_t km_gva_to_kml(uint64_t gva)
{
   if (gva >= GUEST_STACK_START_VA && gva < GUEST_STACK_TOP) {
      return machine.vm_mem_regs[KM_STACK_MEMSLOT]->userspace_addr -
             GUEST_STACK_START_VA + gva;
   }
   if (gva < machine.brk) {
      return machine.vm_mem_regs[KM_TEXT_DATA_MEMSLOT]->userspace_addr + gva;
   }
   errx(1, "km_gva_to_kma: bad guest address 0x%lx", gva);
}

/*
 * Same as above but cast to (void *)
 */
static inline void *km_gva_to_kma(uint64_t gva)
{
   return (void *)km_gva_to_kml(gva);
}

uint64_t km_mem_brk(uint64_t brk);