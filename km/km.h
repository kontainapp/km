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
#include <linux/kvm.h>
#include <sys/param.h>

#include "km_elf.h"

#define rounddown(x, y)  (__builtin_constant_p (y) && powerof2 (y)   \
                         ? ((x) & ~((y) - 1))                       \
                         : (((x) / (y)) * (y)))

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

typedef struct km_vcpu {
   int kvm_vcpu_fd;      // this VCPU file descriptors
   kvm_run_t* cpu_run;   // run control region
} km_vcpu_t;

void km_machine_init(void);
km_vcpu_t* km_vcpu_init(uint64_t ent, uint64_t sp, uint64_t fs_base);
void km_vcpu_run(km_vcpu_t* vcpu);

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
#define KM_MAX_HCALL 512

typedef int (*km_hcall_fn_t)(int hc __attribute__((__unused__)), uint64_t guest_addr, int* status);

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
   uint64_t brk;                                     //
                                                     //
   kvm_cpuid2_t* cpuid;                              // to set VCPUs cpuid
   uint64_t guest_max_physmem;                       // Set from CPUID
   int pdpe1g;                                       //
} km_machine_t;

extern km_machine_t machine;

static const int KM_RSRV_MEMSLOT = 0;
static const int KM_TEXT_DATA_MEMSLOT = 1;
// other text and data memslots follow
static const int KM_STACK_MEMSLOT = KVM_USER_MEM_SLOTS - 1;

#define GUEST_MEM_START_VA memreg_base(KM_TEXT_DATA_MEMSLOT)

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
static const uint64_t GUEST_STACK_TOP = 128 * 1024 * GIB - GIB;
/*
 * Total of all thread stacks, packed into one range of addresses. Stacks
 * (GUEST_STACK_START_SIZE each) will be placed in the area of
 * GUEST_ALL_STACKS_SIZE and the code will control overflow and move/extend in
 * the unlikely event of the need.
 */
static const uint64_t GUEST_ALL_STACKS_SIZE = GIB;
/* Single thread stack size */
static const uint64_t GUEST_STACK_START_SIZE = 2 * MIB;
/* Initial thread stack start - lowest address */
static const uint64_t GUEST_STACK_START_VA =
    GUEST_STACK_TOP - GUEST_STACK_START_SIZE;   // 0x7fffbfe00000

#define GUEST_STACK_START_PA (machine.guest_max_physmem - GUEST_STACK_START_SIZE)

/* Top of the allowed text+data area, or maximum possible value of machine.brk */
#define GUEST_MAX_BRK (machine.guest_max_physmem - GUEST_ALL_STACKS_SIZE)

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
static inline int gva_to_memreg_idx(uint64_t gva)
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

static inline uint64_t memreg_top(int idx)
{
   return (MIB << 1) << idx;
}

static inline uint64_t km_gva_to_kml(uint64_t gva)
{
   if (gva >= GUEST_STACK_START_VA && gva < GUEST_STACK_TOP) {
      return machine.vm_mem_regs[KM_STACK_MEMSLOT]->userspace_addr - GUEST_STACK_START_VA + gva;
   }
   if (GUEST_MEM_START_VA <= gva && gva < machine.brk) {
      int idx = gva_to_memreg_idx(gva);

      return gva - memreg_base(idx) + machine.vm_mem_regs[idx]->userspace_addr;
   }
   return (uint64_t)NULL;
}

/*
 * Same as above but cast to (void *)
 */
static inline void* km_gva_to_kma(uint64_t gva)
{
   return (void*)km_gva_to_kml(gva);
}

uint64_t km_mem_brk(uint64_t brk);
uint64_t km_init_guest(void);

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