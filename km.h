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

typedef int (*km_hcall_fn_t)(void *guest_addr, int *status);

extern km_hcall_fn_t km_hcalls_table[];

void *km_gva_to_kma(uint64_t ga);
uint64_t km_kma_to_gva(void *ka);
uint64_t km_guest_memsize(void);