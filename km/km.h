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
   int kvm_vcpu_fd;      // this VCPU file descriptors
   kvm_run_t* cpu_run;   // run control region
   pthread_t vcpu_thread;
   chan_t* vcpu_chan;
} km_vcpu_t;

void km_machine_init(void);
void km_machine_fini(void);
km_vcpu_t* km_vcpu_init(km_gva_t ent, km_gva_t sp, km_gva_t fs_base);
void* km_vcpu_run_main(void* unused);
void km_vcpu_run(km_vcpu_t* vcpu);

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
#define KM_MAX_HCALL 512
typedef int (*km_hcall_fn_t)(int hc __attribute__((__unused__)),
                             km_hc_args_t* guest_addr,
                             int* status);

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
   int vm_cpu_cnt;                       // count of the below
   km_vcpu_t* vm_vcpus[KVM_MAX_VCPUS];   // VCPUs we created

   kvm_mem_reg_t* vm_mem_regs[KVM_USER_MEM_SLOTS];   // guest physical memory regions
   km_gva_t brk;                                     //
                                                     //
   kvm_cpuid2_t* cpuid;                              // to set VCPUs cpuid
   uint64_t guest_max_physmem;                       // Set from CPUID
   int pdpe1g;                                       //
} km_machine_t;

extern km_machine_t machine;

static inline km_vcpu_t* km_main_vcpu(void)
{
   return machine.vm_vcpus[0];
}

static inline int km_put_new_vcpu(km_vcpu_t* new)
{
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      if (machine.vm_vcpus[i] == NULL) {
         machine.vm_cpu_cnt++;
         machine.vm_vcpus[i] = new;
         return i;
      }
   }
   return -1;
}

static inline void* km_memreg_kma(int idx)
{
   return (km_kma_t)machine.vm_mem_regs[idx]->userspace_addr;
}

static const int KM_RSRV_MEMSLOT = 0;
static const int KM_TEXT_DATA_MEMSLOT = 1;
// other text and data memslots follow
static const int KM_STACK_MEMSLOT = KVM_USER_MEM_SLOTS - 1;

// memreg_base(KM_TEXT_DATA_MEMSLOT)
static const uint64_t GUEST_MEM_START_VA = MIB << KM_TEXT_DATA_MEMSLOT;

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
static const km_gva_t GUEST_STACK_TOP = 128 * 1024 * GIB - GIB;
static const uint64_t GUEST_STACK_SIZE = 2 * MIB;   // Single thread stack size
// Each VCPU, aka thread, gets itw own stack. This is max total size of all of them
static const uint64_t GUEST_STACK_TOTAL_SIZE = GUEST_STACK_SIZE * KVM_MAX_VCPUS;

// Top of the allowed text+data area, or maximum possible value of machine.brk
static inline uint64_t GUEST_MAX_BRK(void)
{
   return machine.guest_max_physmem - GUEST_STACK_TOTAL_SIZE;
}

/*
 * address space is made of exponentially increasing regions (km_cpu_init.c):
 *  base             size  idx   clz
 *   2MB -   4MB      2MB    1    42
 *   4MB -   8MB      4MB    2    41
 *   8MB -  16MB      8MB    3    40
 *  16MB -  32MB     16MB    4    39
 *     ...
 * idx is number of the region, we compute it based on number of leading zeroes
 * in a given address, using clzl instruction.
 * base is address of the first byte in it. Note size equals base.
 *
 * Knowing memory layout and how pml4 is set,
 * convert between guest virtual address and km address.
 */
static inline int gva_to_memreg_idx(km_gva_t gva)
{
   return 43 - __builtin_clzl(gva);
}

/*
 * Note base and size are equal in this layout
 */
static inline uint64_t memreg_base(int idx)
{
   return MIB << idx;
}

static inline uint64_t memreg_size(int idx)
{
   return MIB << idx;
}

static inline km_gva_t memreg_top(int idx)
{
   return (MIB << 1) << idx;
}

static inline km_kma_t km_gva_to_kma(km_gva_t gva)
{
   int idx;

   if (GUEST_STACK_TOP - GUEST_STACK_TOTAL_SIZE <= gva && gva < GUEST_STACK_TOP) {
      idx = KM_STACK_MEMSLOT - (GUEST_STACK_TOP - gva) / GUEST_STACK_SIZE;
      if (machine.vm_mem_regs[idx] == NULL) {
         return NULL;
      }
      return km_memreg_kma(idx) + gva % GUEST_STACK_SIZE;
   }
   if (GUEST_MEM_START_VA <= gva && gva < machine.brk) {
      idx = gva_to_memreg_idx(gva);

      return gva - memreg_base(idx) + km_memreg_kma(idx);
   }
   return NULL;
}

km_gva_t km_mem_brk(km_gva_t brk);
km_gva_t km_stack(void);
km_gva_t km_init_libc_main(void);
int km_create_pthread(pthread_t* restrict pid,
                      const pthread_attr_t* restrict attr,
                      void* (*start)(void*),
                      void* restrict args);

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