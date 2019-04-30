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
#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <linux/kvm.h>

#include "km_elf.h"
#include "km_hcalls.h"

#define rounddown(x, y)                                                                            \
   (__builtin_constant_p(y) && powerof2(y) ? ((x) & ~((y)-1)) : (((x) / (y)) * (y)))

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
   int vcpu_id;             // uniq ID
   kvm_run_t* cpu_run;      // run control region
   pthread_t vcpu_thread;   // km pthread
   pid_t tid;               // Thread Id for VCPU thread. Used to id thread in gdb and reporting
   km_gva_t guest_thr;      // guest pthread
   km_gva_t stack_top;      // available in guest_thr but requres gva_to_kma, save it
   int kvm_vcpu_fd;         // this VCPU file descriptor
   int eventfd;             // gdb uses this to synchronize with VCPU thread
   int is_used;             // 1 means 'busy with workload thread'. 0 means 'ready for reuse'
   int is_paused;           // 1 means the vcpu is waiting for gdb to allow it to continue
   int is_joining;          // 1 if currently joining another thread.
} km_vcpu_t;

// simple enum to help in forcing 'enable/disable' flags
typedef enum {
   KM_FLAG_FORCE_ENABLE = 1,
   KM_FLAG_FORCE_DISABLE = -1,
   KM_FLAG_FORCE_KEEP = 0
} km_flag_force_t;

typedef struct km_machine_init_params {
   uint64_t guest_physmem;         // Requested size of guest physical memory in bytes
   km_flag_force_t force_pdpe1g;   // force on/off 1g pages support regardless of VM CPUID support
                                   // TODO: check if there is a KVM config for force-enable
} km_machine_init_params_t;

void km_machine_init(km_machine_init_params_t* params);
void km_signal_machine_fini(void);
void km_machine_fini(void);
void* km_vcpu_run_main(void* unused);
void* km_vcpu_run(km_vcpu_t* vcpu);

void km_hcalls_init(void);
void km_hcalls_fini(void);

/*
 * kernel include/linux/kvm_host.h
 */
static const int CPUID_ENTRIES = 100;   // A little padding, kernel says 80
#define KVM_MAX_VCPUS 288

#define KM_MEM_SLOTS 41   // We use 35 on 512GB machine, 41 on 4TB, out of 509 KVM_USER_MEM_SLOTS

typedef struct km_machine {
   int kvm_fd;                                // /dev/kvm file descriptor
   int mach_fd;                               // VM file descriptor
   size_t vm_run_size;                        // size of the run control region
                                              //
   int vm_vcpu_run_cnt;                       // count of still running VCPUs
   km_vcpu_t* vm_vcpus[KVM_MAX_VCPUS];        // VCPUs we created
                                              //
   kvm_mem_reg_t vm_mem_regs[KM_MEM_SLOTS];   // guest physical memory regions
   km_gva_t brk;                 // program break (highest address in bottom VA, i.e. txt/data)
   km_gva_t tbrk;                // top break (lowest address in top VA)
   pthread_mutex_t brk_mutex;    // protects the two above
                                 //
   kvm_cpuid2_t* cpuid;          // to set VCPUs cpuid
   uint64_t guest_max_physmem;   // Set from CPUID
   int pdpe1g;                   // 1 if 1G pages are supported by HW
   // derivatives from guest_max_physmem  and memory layout. Cached to save on recalculation
   uint64_t guest_mid_physmem;   // first byte of the top half of PA
   int mid_mem_idx;              // idx for the last region in the bottom half of PA
   int last_mem_idx;             // idx for the last (and hidden) region in the top half of PA
   // syncronization support
   int shutdown_fd;       // eventfd to coordinate final shutdown
   int exit_group;        // 1 if processing exit_group() call now.
   int pause_requested;   // 1 if all VCPUs are being paused. Used to prevent race with new vcpu threads
   int ret;               // return code from payload's main thread
} km_machine_t;

extern km_machine_t machine;

static inline void km_mem_lock(void)
{
   if (pthread_mutex_lock(&machine.brk_mutex) != 0) {
      err(1, "memory lock failed");
   }
}

static inline void km_mem_unlock(void)
{
   pthread_mutex_unlock(&machine.brk_mutex);
}

static inline km_vcpu_t* km_main_vcpu(void)
{
   return machine.vm_vcpus[0];
}

static inline int km_wait_on_eventfd(int fd)
{
   eventfd_t value;
   while (eventfd_read(fd, &value) == -1 && errno == EINTR)
      ;
   return value;
}

void km_init_libc_main(km_vcpu_t* vcpu, int argc, char* const argv[]);
int km_pthread_create(
    km_vcpu_t* vcpu, pthread_t* restrict pid, const km_kma_t attr, km_gva_t start, km_gva_t args);
int km_pthread_join(km_vcpu_t* vcpu, pthread_t pid, km_kma_t ret);
void km_pthread_fini(km_vcpu_t* vcpu);

void km_vcpu_stopped(km_vcpu_t* vcpu);
km_vcpu_t* km_vcpu_get(void);
void km_vcpu_put(km_vcpu_t* vcpu);
int km_vcpu_set_to_run(km_vcpu_t* vcpu, int is_pthread);
void km_vcpu_detach(km_vcpu_t* vcpu);

typedef int (*km_vcpu_apply_cb)(km_vcpu_t* vcpu, uint64_t data);   // return 0 if all is good
extern int km_vcpu_apply_all(km_vcpu_apply_cb func, uint64_t data);
extern int km_vcpu_pause(km_vcpu_t* vcpu, uint64_t unused);
extern void km_vcpu_wait_for_all_to_pause(void);
extern int km_vcpu_print(km_vcpu_t* vcpu, uint64_t unused);
extern km_vcpu_t* km_vcpu_fetch(int id);
extern km_vcpu_t* km_vcpu_fetch_by_tid(int tid);

extern pid_t gettid(void);

#define KM_SIGVCPUSTOP SIGUSR1   //  After km start, used to signal VCP thread to force KVM exit

/*
 * To check for success/failure from plain system calls and similar logic, returns -1 if fail.
 * *NOT* to be used for actual syscall wrapper as it doesn't set errno.
 */
static inline long km_syscall_ok(uint64_t r)
{
   return r > -4096ul ? -1 : r;
}

/*
 * Trivial trace control - with switch to turn on/off and on and a tag to match.
 * E.g. "-Vgdb" will only match GDB related messages, and '-V(gdb|kvm)' will match both gdb and
 * kvm-related messages. The "gdb" "kvm" tag is passed to km_info API
 */
typedef struct km_info_trace {
   regex_t tags;   // only trace the tags matching this regexp
   enum {
      KM_TRACE_NONE,
      KM_TRACE_INFO,
      KM_TRACE_WARN,
      KM_TRACE_ERR
   } level;   // trace level. using only KM_TRACE_NONE for now
} km_info_trace_t;
extern km_info_trace_t km_info_trace;

#define km_trace_enabled() (km_info_trace.level != KM_TRACE_NONE)   // 1 for yes, 0 for no
#define km_info(tag, ...)                                                                          \
   do {                                                                                            \
      if (km_trace_enabled() && regexec(&km_info_trace.tags, tag, 0, NULL, 0) == 0)                \
         warn(__VA_ARGS__);                                                                        \
   } while (0)
#define km_infox(tag, ...)                                                                         \
   do {                                                                                            \
      if (km_trace_enabled() && regexec(&km_info_trace.tags, tag, 0, NULL, 0) == 0)                \
         warnx(__VA_ARGS__);                                                                       \
   } while (0)

// tags for different traces
#define KM_TRACE_VCPU "vcpu"
#define KM_TRACE_KVM "kvm"
#define KM_TRACE_MEM "mem"

#endif /* #ifndef __KM_H__ */