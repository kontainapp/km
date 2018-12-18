/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#include <stdint.h>
#include <linux/kvm.h>
#include <err.h>

static const int PAGE_SIZE = 0x1000;                     // standard 4k page
static const int HUGE_PAGE_SIZE = PAGE_SIZE * 512;       // 2MB

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

int load_elf(const char *file, void *mem, uint64_t *entry);
void km_machine_init(void);
km_vcpu_t *km_vcpu_init(uint64_t ent, uint64_t sp);
void km_vcpu_run(km_vcpu_t *vcpu);

typedef int (*km_hcall_fn_t)(uint64_t guest_addr, int *status);

extern km_hcall_fn_t km_hcalls_table[];

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

typedef enum {
   KM_MEMSLOT_BASE = 0,
   KM_RSRV_MEMSLOT = KM_MEMSLOT_BASE,
   KM_GUEST_MEMSLOT,
   KM_MEMSLOT_CNT
} km_memslot_t;

extern km_machine_t machine;

static const int GUEST_MEM_START = PAGE_SIZE * 512;                    // 2MB
static const uint64_t GUEST_MEM_SIZE = PAGE_SIZE * 1024 * 256ul;       // 1GB

/*
 * Knowing memory layout and how pml4 is set,
 * convert between guest virtual address and km address
 */
static inline uint64_t km_gva_to_kma(uint64_t ga)
{
   if (ga >= GUEST_MEM_SIZE) {
      errx(1, "km_gva_to_kma: bad guest address 0x%lx", ga);
   }
   return machine.vm_mem_regs[KM_GUEST_MEMSLOT]->userspace_addr + ga;
}

uint64_t km_kma_to_gva(void *ka);
uint64_t km_guest_memsize(void);