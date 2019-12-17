/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
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
#include <signal.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <linux/kvm.h>

#include "bsd_queue.h"
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
typedef struct kvm_vcpu_events kvm_vcpu_events_t;

typedef uint64_t km_gva_t;   // guest virtual address (i.e. address in payload space)
typedef void* km_kma_t;      // kontain monitor address (i.e. address in km process space)

typedef uint64_t km_sigset_t;

// must match k_sigaction
typedef struct km_sigaction {
   km_gva_t handler;
   uint32_t sa_flags;
   km_gva_t restorer;
   km_sigset_t sa_mask;
} km_sigaction_t;

typedef struct km_signal {
   TAILQ_ENTRY(km_signal) link;
   siginfo_t info;
} km_signal_t;

typedef struct km_signal_list {
   TAILQ_HEAD(, km_signal) head;
   sigset_t mask;   // Signals in list.
} km_signal_list_t;

typedef unsigned long int pthread_tid_t;

/*
 * This enum describes states gdb can assign to a thread with the vCont
 * remote protocol command.
 * This defines what state the gdb client would like the thread to be
 * in when the payload is "running".
 * It may seem that there is overlap between the gdb state of a thread
 * and the vcpu's state as defined in km_vcpu but gdb_run_state_t just
 * defines gdb's intent for the thread whereas the km_vcpu state defines
 * what the vcpu is currently doing.
 */
typedef enum {
   GRS_NONE,             // no state has been assigned
   GRS_PAUSED,           // gdb wants this thread paused
   GRS_STEPPING,         // gdb wants this thread single stepping
   GRS_RANGESTEPPING,    // gdb wants this thread stepping through a range of addresses
   GRS_RUNNING,          // gdb wants this thread running
} gdb_run_state_t;

/*
 * gdb related state stored in the km_vcpu_t.
 */
typedef struct {
   gdb_run_state_t gvs_gdb_run_state; // parked, paused, stepping, running
   km_gva_t gvs_steprange_start;      // beginning address of the address range to step through
   km_gva_t gvs_steprange_end;        // end address of the address range to step through
} gdb_vcpu_state_t;

typedef struct km_vcpu {
   int vcpu_id;                   // uniq ID
   int kvm_vcpu_fd;               // this VCPU file descriptor
   kvm_run_t* cpu_run;            // run control region
   pthread_t vcpu_thread;         // km pthread
   pthread_mutex_t thr_mtx;       // protects the three fields below
   pthread_cond_t thr_cv;         // used by vcpu_pthread to block while vcpu isn't in use
                                  //
   km_gva_t guest_thr;            // guest pthread, FS reg in the guest
   km_gva_t stack_top;            // available in guest_thr but requres gva_to_kma, save it
                                  //
   int gdb_efd;                   // gdb uses this to synchronize with VCPU thread
   int is_used;                   // 1 means 'busy with workload thread'. 0 means 'ready for reuse'
   int is_paused;                 // 1 means the vcpu is waiting for gdb to allow it to continue
   pthread_tid_t joining_pid;     // pid if currently joining another thread pid, -1 if not
   km_gva_t exit_res;             // exit status for this thread
   int regs_valid;                // Are registers valid?
   kvm_regs_t regs;               // Cached register values.
   int sregs_valid;               // Are segment registers valid?
   kvm_sregs_t sregs;             // Cached segment register values.
   km_sigset_t sigmask;           // blocked signals for thread
   km_signal_list_t sigpending;   // List of signals sent to thread

   /*
    * Linux/Pthread handshake hacks. These are actually part of the standard.
    */
   km_gva_t set_child_tid;        // See 'man 2 set_child_tid' for details
   km_gva_t clear_child_tid;      // See 'man 2 set_child_tid' for details

   uint64_t dr_regs[4];           // remember the addresses we are watching and have written into
                                  // the processor's debugging facilities in DR0 - DR3.

   gdb_vcpu_state_t gdb_vcpu_state; // gdb's per thread (vcpu) state.
} km_vcpu_t;

/*
 * Produce tid for this vcpu. This should match km_vcpu_fetch_by_tid()
 */
static inline pid_t km_vcpu_get_tid(km_vcpu_t* vcpu)
{
   return vcpu->vcpu_id + 1;
}

// simple enum to help in forcing 'enable/disable' flags
typedef enum {
   KM_FLAG_FORCE_ENABLE = 1,
   KM_FLAG_FORCE_DISABLE = -1,
   KM_FLAG_FORCE_KEEP = 0
} km_flag_force_t;

// struct for passing command line / config information into different inits.
typedef struct km_machine_init_params {
   uint64_t guest_physmem;         // Requested size of guest physical memory in bytes
   km_flag_force_t force_pdpe1g;   // force on/off 1g pages support regardless of VM CPUID support
   km_flag_force_t overcommit_memory;   // memory overcommit (i.e. MAP_NORESERVE in mmap)
                                        // Note: if too much of it is accessed, we expect Linux OOM
                                        // killer to kick in
} km_machine_init_params_t;

void km_machine_init(km_machine_init_params_t* params);
void km_signal_machine_fini(void);
void km_machine_fini(void);
void* km_vcpu_run_main(void* unused);
void* km_vcpu_run(km_vcpu_t* vcpu);
void km_dump_vcpu(km_vcpu_t* vcpu);
void km_read_registers(km_vcpu_t* vcpu);
void km_write_registers(km_vcpu_t* vcpu);
void km_read_sregisters(km_vcpu_t* vcpu);
void km_write_sregisters(km_vcpu_t* vcpu);

void km_hcalls_init(void);
void km_hcalls_fini(void);

typedef struct km_filesys {
   int* guestfd_to_hostfd_map;   // file descriptor map
   int* hostfd_to_guestfd_map;   // reverse file descriptor map
   char** guestfd_to_name_map;   // guest file name
   int nfdmap;                   // size of file descriptor maps
} km_filesys_t;

// mmaps lists typedefs
TAILQ_HEAD(km_mmap_list, km_mmap_reg);
typedef struct km_mmap_list km_mmap_list_t;

// mmap region monitor internal state
typedef union {
   struct {
      uint32_t km_mmap_inited : 1;    // 1 if area was zeroed out already
      uint32_t km_mmap_monitor : 1;   // 1 if area is allocated by monitor
      uint32_t km_unused : 30;
   };
   uint32_t data32;
} km_mmap_flags_u;

// single mmap-ed (or munmapped) region
typedef struct km_mmap_reg {
   km_gva_t start;
   size_t size;
   int flags;                  // flag as passed to mmap()
   int protection;             // as passed to mmaps() or mprotect(), or 0 for unmapped region
   km_mmap_flags_u km_flags;   // Flags used by KM and not by guest
   char* filename;
   off_t offset;   // offset into fd (if it exists).
   TAILQ_ENTRY(km_mmap_reg) link;
} km_mmap_reg_t;

// mmaps control block
typedef struct km_mmap_cb {   // control block
   km_mmap_list_t free;       // list of free regions
   km_mmap_list_t busy;       // list of mapped regions
   pthread_mutex_t mutex;     // global map lock
} km_mmap_cb_t;

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
   int intr_fd;           // eventfd used to signal to listener that a vCPU stopped
   int shutdown_fd;       // eventfd to coordinate final shutdown
   int exit_group;        // 1 if processing exit_group() call now.
   int pause_requested;   // 1 if all VCPUs are being paused. Used to prevent race with new vcpu threads
   int exit_status;       // return code from payload's main thread
   // guest interrupt support
   km_gva_t gdt;                   // Guest address of Global Descriptor Table (GDT)
   size_t gdt_size;                // GDT size (bytes)
   km_gva_t idt;                   // Guest address of Interrupt Descriptor Table (IDT)
   size_t idt_size;                // IDT size (bytes)
   pthread_mutex_t signal_mutex;   // Protect signal data structures.
   km_signal_list_t sigpending;    // List of signals pending for guest
   km_signal_list_t sigfree;       // Freelist of signal entries.
   km_sigaction_t sigactions[_NSIG];
   km_filesys_t filesys;
   km_mmap_cb_t mmaps;   // guest memory regions managed with mmaps/mprotect/munmap
   void* auxv;           // Copy of process AUXV (used if core is dumped)
   size_t auxv_size;     // size of process AUXV (used if core is dumped)
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

static inline void km_signal_lock(void)
{
   if (pthread_mutex_lock(&machine.signal_mutex) != 0) {
      err(1, "signal lock failed");
   }
}

static inline void km_signal_unlock(void)
{
   pthread_mutex_unlock(&machine.signal_mutex);
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

km_gva_t km_init_main(km_vcpu_t* vcpu, int argc, char* const argv[], int envc, char* const envp[]);
int km_pthread_create(
    km_vcpu_t* vcpu, pthread_tid_t* restrict pid, const km_kma_t attr, km_gva_t start, km_gva_t args);
int km_pthread_join(km_vcpu_t* vcpu, pthread_tid_t pid, km_kma_t ret);

int km_clone(km_vcpu_t* vcpu,
             unsigned long flags,
             uint64_t child_stack,
             km_gva_t ptid,
             km_gva_t ctid,
             unsigned long newtls,
             void** cargs);
uint64_t km_set_tid_address(km_vcpu_t* vcpu, km_gva_t tidptr);
void km_exit(km_vcpu_t* vcpu, int status);

void km_vcpu_stopped(km_vcpu_t* vcpu);
km_vcpu_t* km_vcpu_get(void);
void km_vcpu_put(km_vcpu_t* vcpu);
int km_vcpu_set_to_run(km_vcpu_t* vcpu, km_gva_t start, uint64_t arg1, uint64_t arg2);
void km_vcpu_detach(km_vcpu_t* vcpu);

typedef int (*km_vcpu_apply_cb)(km_vcpu_t* vcpu, uint64_t data);   // return 0 if all is good
extern int km_vcpu_apply_all(km_vcpu_apply_cb func, uint64_t data);
extern int km_vcpu_pause(km_vcpu_t* vcpu, uint64_t unused);
extern void km_vcpu_wait_for_all_to_pause(void);
extern int km_vcpu_print(km_vcpu_t* vcpu, uint64_t unused);
extern km_vcpu_t* km_vcpu_fetch(pthread_tid_t);
extern km_vcpu_t* km_vcpu_fetch_by_tid(int tid);

// Interrupt handling.
void km_init_guest_idt(km_gva_t handlers);
void km_handle_interrupt(km_vcpu_t* vcpu);

#define KM_SIGVCPUSTOP SIGUSR1   //  After km start, used to signal VCP thread to force KVM exit

/*
 * To check for success/failure from plain system calls and similar logic, returns -1 and sets errno
 * if fail.
 */
static inline long km_syscall_ok(uint64_t r)
{
   if (r > -4096ul) {
      errno = -r;
      return -1;
   }
   return r;
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
#define KM_TRACE_MMAP "mmap"
#define KM_TRACE_COREDUMP "coredump"
#define KM_TRACE_SIGNALS "signals"

#endif /* #ifndef __KM_H__ */
