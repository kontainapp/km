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

#ifndef __KM_H__
#define __KM_H__

#include <err.h>
#include <stdint.h>
#include <sys/param.h>
#include <linux/kvm.h>

#include "chan/chan.h"
#include "km_elf.h"
#include "km_hcalls.h"

#define rounddown(x, y)                                                                            \
   (__builtin_constant_p(y) && powerof2(y) ? ((x) & ~((y)-1)) : (((x) / (y)) * (y)))

static const uint64_t PAGE_SIZE = 0x1000;   // standard 4k page
static const uint64_t MIB = 0x100000;       // MByte
static const uint64_t GIB = 0x40000000ul;   // GByte

/*
 * Types from linux/kvm.h
 */
typedef struct kvm_userspace_memory_region kvm_mem_reg_t;
typedef struct kvm_run kvm_run_t;
typedef struct kvm_cpuid2 kvm_cpuid2_t;
typedef struct kvm_segment kvm_seg_t;
typedef struct kvm_sregs kvm_sregs_t;
typedef struct kvm_regs kvm_regs_t;

typedef uint64_t km_gva_t;   // guest virtual address (i.e. address in payload space)
typedef void* km_kma_t;      // kontain monitor address (i.e. address in km process space)

typedef struct km_vcpu {
   int kvm_vcpu_fd;         // this VCPU file descriptor
   int vcpu_id;             // uniq ID
   kvm_run_t* cpu_run;      // run control region
   pthread_t vcpu_thread;   // km pthread
   km_gva_t guest_thr;      // guest pthread
   km_gva_t stack_top;      // available in guest_thr but requres gva_to_kma, save it
   km_gva_t map_base;       // start of the guest mmap region used for stack
   size_t map_size;         // size of the guest mmap region used for stack
   chan_t* vcpu_chan;
} km_vcpu_t;

void km_machine_init(void);
void km_signal_machine_fini(void);
void km_machine_fini(void);
km_vcpu_t* km_vcpu_init(int is_pthread, km_gva_t sp, km_gva_t fs_base);
void km_vcpu_fini(km_vcpu_t* vcpu);
void* km_vcpu_run_main(void* unused);
void* km_vcpu_run(km_vcpu_t* vcpu);

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
typedef int (*km_hcall_fn_t)(int hc __attribute__((__unused__)), km_hc_args_t* guest_addr, int* status);

extern km_hcall_fn_t km_hcalls_table[];

void km_hcalls_init(void);
void km_hcalls_fini(void);

/*
 * kernel include/linux/kvm_host.h
 */
static const int CPUID_ENTRIES = 100;   // A little padding, kernel says 80
#define KVM_USER_MEM_SLOTS 509
#define KVM_MAX_VCPUS 288

typedef struct km_machine {
   int kvm_fd;                           // /dev/kvm file descriptor
   int mach_fd;                          // VM file descriptor
   size_t vm_run_size;                   // size of the run control region
                                         //
   int vm_vcpu_run_cnt;                  // count of still running VCPUs
   km_vcpu_t* vm_vcpus[KVM_MAX_VCPUS];   // VCPUs we created

   kvm_mem_reg_t* vm_mem_regs[KVM_USER_MEM_SLOTS];   // guest physical memory regions
   km_gva_t brk;    // program break (highest address in bottom VA, i.e. txt/data)
   km_gva_t tbrk;   // top break (lowest address in top VA)

   kvm_cpuid2_t* cpuid;          // to set VCPUs cpuid
   uint64_t guest_max_physmem;   // Set from CPUID
   int pdpe1g;                   // 1 if 1G pages are supported by HW

   // derivatives from guest_max_physmem  and memory layout. Cached to save on recalculation
   uint64_t guest_mid_physmem;   // first byte of the top half of PA
   int mid_mem_idx;              // idx for the last region in the bottom half of PA
   int last_mem_idx;             // idx for the last (and hidden) region in the top half of PA
} km_machine_t;

extern km_machine_t machine;

static inline km_vcpu_t* km_main_vcpu(void)
{
   return machine.vm_vcpus[0];
}

km_gva_t km_init_libc_main(void);
int km_create_pthread(pthread_t* restrict pid, const km_kma_t attr, km_gva_t start, km_gva_t args);
int km_join_pthread(pthread_t pid, km_kma_t ret);
void km_vcpu_stopped(km_vcpu_t* vcpu);

/*
 * Trivial trace() based on warn() - but with an a switch to run off and on.
 */
extern int g_km_info_verbose;
#define km_info(...)                                                                               \
   do {                                                                                            \
      if (g_km_info_verbose)                                                                       \
         warn(__VA_ARGS__);                                                                        \
   } while (0)
#define km_infox(...)                                                                              \
   do {                                                                                            \
      if (g_km_info_verbose)                                                                       \
         warnx(__VA_ARGS__);                                                                       \
   } while (0)

#endif /* #ifndef __KM_H__ */