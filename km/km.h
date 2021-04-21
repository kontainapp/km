/*
 * Copyright © 2018-2021 Kontain Inc. All rights reserved.
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

#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <linux/kvm.h>

#include "bsd_queue.h"
#include "km_elf.h"
#include "km_hcalls.h"

#define rounddown(x, y)                                                                            \
   (__builtin_constant_p(y) && powerof2(y) ? ((x) & ~((y)-1)) : (((x) / (y)) * (y)))

typedef const char* const const_string_t;

/*
 * Types from linux/kvm.h
 */
typedef struct kvm_userspace_memory_region kvm_mem_reg_t;
typedef struct kvm_run kvm_run_t;
typedef struct kvm_cpuid2 kvm_cpuid2_t;
typedef struct kvm_segment kvm_seg_t;
typedef struct kvm_sregs kvm_sregs_t;
typedef struct kvm_regs kvm_regs_t;
typedef struct kvm_xcrs kvm_xcrs_t;
typedef struct kvm_vcpu_events kvm_vcpu_events_t;

static const uint64_t FAILED_GA = -1ul;   // indicate bad guest address
typedef uint64_t km_gva_t;                // guest virtual address (i.e. address in payload space)
typedef uint64_t km_gpa_t;                // guest physical address (i.e. address in payload space)
typedef void* km_kma_t;   // kontain monitor address (i.e. address in km process space)

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

typedef stack_t km_stack_t;

typedef unsigned long int pthread_tid_t;

/*
 * When a thread hits a breakpoint, or a single step operation completes, or a signal is generated,
 * a gdb event is queued by the thread and then the gdb server is woken.  The gdb server then
 * processes the elements in the queue.  This structure describes a thread's gdb event.
 */
typedef struct gdb_event {
   TAILQ_ENTRY(gdb_event) link;   // link field for being in gdb's event queue
   uint8_t entry_is_active;       // to prevent us from overwriting a thread's single gdb_event
   pid_t sigthreadid;             // the thread this entry belongs to
   int signo;                     // the reason a thread has generated a gdb event
   int exit_reason;               // the reason from kvm for exiting the guest vcpu
} gdb_event_t;

/*
 * This enum describes states gdb can assign to a thread with the vCont remote protocol command.
 * This defines what state the gdb client would like the thread to be in when the payload is
 * "running". It may seem that there is overlap between the gdb state of a thread and the vcpu's
 * state as defined in km_vcpu but gdb_run_state_t just defines gdb's intent for the thread whereas
 * the km_vcpu state defines what the vcpu is currently doing.
 */
typedef enum {
   THREADSTATE_NONE,            // no state has been assigned
   THREADSTATE_PAUSED,          // gdb wants this thread paused
   THREADSTATE_STEPPING,        // gdb wants this thread single stepping
   THREADSTATE_RANGESTEPPING,   // gdb wants this thread stepping through a range of addresses
   THREADSTATE_RUNNING,         // gdb wants this thread running
} gdb_thread_state_t;

/*
 * gdb related state stored in the km_vcpu_t.
 */
typedef struct {
   gdb_thread_state_t gdb_run_state;   // thread is paused, stepping, or running
   km_gva_t steprange_start;           // beginning address of the address range to step through
   km_gva_t steprange_end;             // end address of the address range to step through
   gdb_event_t event;                  // the thread event that has woken the gdb server
} gdb_vcpu_state_t;

typedef struct km_vcpu_list {
   SLIST_HEAD(, km_vcpu) head;
} km_vcpu_list_t;

// VPCU state
typedef enum {
   PARKED_IDLE = 0,   // Idle, parked for reuse. Thread waits on thr_cv or doesn't exist. VCPU
                      // queued in SLIST off of machine.vm_idle_vcpus
   STARTING,   // Being initialized. Thread waits on thr_cv or doesn't exist. VCPU removed from the SLIST
   HYPERCALL,   // Running in KM
   HCALL_INT,   // Hypercall was interrupted by KM_SIGVCPUSTOP
   IN_GUEST,    // Running in ioctl( KVM_RUN )
   PAUSED       // Paused in km_vcpu_handle_pause
} __attribute__((__packed__)) km_vcpu_state_t;

typedef struct km_vcpu {
   int vcpu_id;                        // uniq ID
   int kvm_vcpu_fd;                    // this VCPU file descriptor
   kvm_run_t* cpu_run;                 // run control region
   pthread_t vcpu_thread;              // km pthread
   pthread_mutex_t thr_mtx;            // protects the three fields below
   pthread_cond_t thr_cv;              // used by vcpu_pthread to block while vcpu isn't in use
   uint16_t hypercall;                 // hypercall #
   uint8_t restart;                    // hypercall needs restarting
   km_vcpu_state_t state;              // state
   uint8_t regs_valid;                 // Are registers valid?
   uint8_t sregs_valid;                // Are segment registers valid?
   uint8_t in_sigsuspend;              // if true thread is running in the sigsuspend() hypercall
   uint8_t hypercall_returns_signal;   // if true a hypercall is returning a signal directly to the
                                       // caller so there is no need to setup the signal handler
   //
   // When vcpu is used (i.e. not PARKED_IDLE) the field is used for stack_top.
   // PARKED_IDLE vcpus are queued in SLIST using next_idle as stack_top isn't needed
   union {
      km_gva_t stack_top;               // also available in guest_thr
      SLIST_ENTRY(km_vcpu) next_idle;   // next in idle list
   };
   km_gva_t guest_thr;                 // guest pthread, FS reg in the guest
   km_stack_t sigaltstack;             //
   km_gva_t mapself_base;              // delayed unmap address
   size_t mapself_size;                // and size
                                       //
   kvm_regs_t regs;                    // Cached register values.
   kvm_sregs_t sregs;                  // Cached segment register values.
   km_sigset_t sigmask;                // blocked signals for thread
   km_signal_list_t sigpending;        // List of signals sent to thread
   pthread_cond_t signal_wait_cv;      // wait for signals with this cv
   km_sigset_t saved_sigmask;          // sigmask saved by sigsuspend()
   TAILQ_ENTRY(km_vcpu) signal_link;   // link for signal waiting queue
   /*
    * Linux/Pthread handshake hacks. These are actually part of the standard.
    */
   km_gva_t set_child_tid;     // See 'man 2 set_child_tid' for details
   km_gva_t clear_child_tid;   // See 'man 2 set_child_tid' for details

   uint64_t dr_regs[4];   // remember the addresses we are watching and have written into
                          // the processor's debugging facilities in DR0 - DR3.
   gdb_vcpu_state_t gdb_vcpu_state;   // gdb's per thread (vcpu) state.
} km_vcpu_t;

static inline int km_on_altstack(km_vcpu_t* vcpu, km_gva_t sp)
{
   return (km_gva_t)vcpu->sigaltstack.ss_sp <= sp &&
          sp < (km_gva_t)vcpu->sigaltstack.ss_sp + vcpu->sigaltstack.ss_size;
}

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
   KM_FLAG_FORCE_KEEP = 0,
} km_flag_force_t;

// struct for passing command line / config information into different inits.
typedef struct km_machine_init_params {
   uint64_t guest_physmem;         // Requested size of guest physical memory in bytes
   km_flag_force_t force_pdpe1g;   // force on/off 1g pages support regardless of VM CPUID support
   km_flag_force_t overcommit_memory;   // memory overcommit (i.e. MAP_NORESERVE in mmap)
                                        // Note: if too much of it is accessed, we expect Linux
                                        // OOM killer to kick in
   char* vdev_name;   // Device name. Virtualization type is defined by ioctl after this file is open
} km_machine_init_params_t;
extern km_machine_init_params_t km_machine_init_params;

void km_machine_setup(km_machine_init_params_t* params);
void km_machine_init_pidinfo(pid_t ppid, pid_t pid, pid_t next_pid);
void km_machine_init(km_machine_init_params_t* params);
void km_signal_machine_fini(void);
void km_vcpu_fini(km_vcpu_t* vcpu);
void km_machine_fini(void);
void kvm_vcpu_init_sregs(km_vcpu_t* vcpu);
void km_start_all_vcpus(void);
void km_start_vcpus();
void* km_vcpu_run(km_vcpu_t* vcpu);
int km_run_vcpu_thread(km_vcpu_t* vcpu);
void km_dump_vcpu(km_vcpu_t* vcpu);
void km_read_registers(km_vcpu_t* vcpu);
void km_write_registers(km_vcpu_t* vcpu);
void km_read_sregisters(km_vcpu_t* vcpu);
void km_write_sregisters(km_vcpu_t* vcpu);
void km_read_xcrs(km_vcpu_t* vcpu);
void km_write_xcrs(km_vcpu_t* vcpu);

void km_hcalls_init(void);
void km_hcalls_fini(void);

/*
 * Actual `struct km_filesys` format is private to km_filesys.c
 */
struct km_filesys;
typedef struct km_filesys* km_filesys_ptr_t;

// mmaps lists typedefs
TAILQ_HEAD(km_mmap_list, km_mmap_reg);
typedef struct km_mmap_list km_mmap_list_t;

// mmap region monitor internal state
typedef union {
   struct {
      uint32_t km_mmap_monitor : 1;   // 1 if area is allocated by monitor
      uint32_t km_mmap_clean : 1;     // doesn't need zeroing when exposed to payload
      uint32_t km_mmap_part_of_monitor : 1;
      uint32_t km_unused : 29;
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
   int recovery_mode;         // disable region consolidation
} km_mmap_cb_t;

// enumerate type of virtual machine
typedef enum vm_type {
   VM_TYPE_KVM = 0,   // Kernel Virtual Machine
   VM_TYPE_KKM        // Kontain Kernel Module
} vm_type_t;

static const_string_t DEVICE_KONTAIN = "/dev/kontain";
static const_string_t DEVICE_KVM = "/dev/kvm";
static const_string_t DEVICE_KKM = "/dev/kkm";

/*
 * kernel include/linux/kvm_host.h
 */
static const int CPUID_ENTRIES = 100;   // A little padding, kernel says 80
#define KVM_MAX_VCPUS 288

/*
 * We use 36 on 512GB machine, 42 on 4TB, out of 509 KVM_USER_MEM_SLOTS slot 0 is used for pages
 * tables and some other things slot 41 is used to map the vdso and vvar pages into the payload
 * address space
 * Slot 42 is used to map code that is part of km into the guest address space.
 */
#define KM_MEM_SLOTS 43

typedef struct km_machine {
   int kvm_fd;                                // /dev/kvm file descriptor
   vm_type_t vm_type;                         // VM type kvm or kkm
   int mach_fd;                               // VM file descriptor
   size_t vm_run_size;                        // size of the run control region
                                              //
   pthread_mutex_t vm_vcpu_mtx;               // serialize vcpu start/stop, protects four below
   int vm_vcpu_run_cnt;                       // count of still running VCPUs
   int vm_vcpu_cnt;                           // count of allocated VCPUs
   km_vcpu_t* vm_vcpus[KVM_MAX_VCPUS];        // VCPUs we created
   km_vcpu_list_t vm_idle_vcpus;              // Parked vcpu ready for reuse
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
   int intr_fd;                  // eventfd used to signal to listener that a vCPU stopped
   int shutdown_fd;              // eventfd to coordinate final shutdown
   int exit_group;               // 1 if processing exit_group() call now.
   int pause_requested;   // 1 if all VCPUs are being paused. Used to prevent race with new vcpu threads
   int exit_status;       // return code from payload's main thread
   pthread_mutex_t pause_mtx;   // protects .pause_requested and indirectly gdb_run_state in vcpus
   pthread_cond_t pause_cv;     // vcpus wait on it when pause_requested gdb_run_state say pause
                                // guest interrupt support
   km_gva_t gdt;                // Guest address of Global Descriptor Table (GDT)
   size_t gdt_size;             // GDT size (bytes)
   km_gva_t idt;                // Guest address of Interrupt Descriptor Table (IDT)
   size_t idt_size;             // IDT size (bytes)
   pthread_mutex_t signal_mutex;   // Protect signal data structures.
   km_signal_list_t sigpending;    // List of signals pending for guest
   km_signal_list_t sigfree;       // Freelist of signal entries.
   km_sigaction_t sigactions[_NSIG];
   km_filesys_ptr_t filesys;
   km_mmap_cb_t mmaps;   // guest memory regions managed with mmaps/mprotect/munmap
   void* auxv;           // Copy of process AUXV (used if core is dumped)
   size_t auxv_size;     // size of process AUXV (used if core is dumped)
   pid_t ppid;           // parent pid, 1 for the leader
   pid_t pid;            // the payload's km pid
   pid_t next_pid;       // the pid for a forked payload process

   // VM Driver Specific Information
   union {
      // KVM specific data
      struct {
         uint8_t xsave;   // Is KVM_GET_XSAVE supported.
      } kvm;
      // TBD Add KKM specific data (if any).
      int dummy;
   } vmtype_u;
} km_machine_t;

extern km_machine_t machine;

static inline km_vcpu_t* km_main_vcpu(void)
{
   return machine.vm_vcpus[0];
}

static inline int km_wait_on_eventfd(int fd)
{
   eventfd_t value;
   while (eventfd_read(fd, &value) == -1 && errno == EINTR) {
      ;
   }
   return value;
}

km_gva_t km_init_main(km_vcpu_t* vcpu, int argc, char* const argv[], int envc, char* const envp[]);

int km_clone(km_vcpu_t* vcpu,
             unsigned long flags,
             uint64_t child_stack,
             km_gva_t ptid,
             km_gva_t ctid,
             unsigned long newtls);
uint64_t km_set_tid_address(km_vcpu_t* vcpu, km_gva_t tidptr);
void km_exit(km_vcpu_t* vcpu);

void km_vcpu_stopped(km_vcpu_t* vcpu);
km_vcpu_t* km_vcpu_get(void);
km_vcpu_t* km_vcpu_restore(int tid);   // Used by snapshot restore
void km_vcpu_put(km_vcpu_t* vcpu);
int km_vcpu_set_to_run(km_vcpu_t* vcpu, km_gva_t start, uint64_t arg);
int km_vcpu_clone_to_run(km_vcpu_t* vcpu, km_vcpu_t* new_vcpu);
void km_vcpu_detach(km_vcpu_t* vcpu);

typedef int (*km_vcpu_apply_cb)(km_vcpu_t* vcpu, void* data);
int km_vcpu_apply_all(km_vcpu_apply_cb func, void* data);
int km_vcpu_count(void);

typedef enum {
   GUEST_ONLY,   // signal only IN_GUEST vcpus
   ALL,          // signal all vcpus
} km_pause_t;

void km_vcpu_pause_all(km_vcpu_t* vcpu, km_pause_t type);
km_vcpu_t* km_vcpu_fetch_by_tid(int tid);

static inline void km_vcpu_sync_rip(km_vcpu_t* vcpu)
{
   /*
    * This is to sync the registers, specifically RIP, with KVM.
    * Turns out there is a difference between KVM or azure and local machines. When we are in
    * hypercall the RIP points to the OUT instruction (local machine) or the next one (azure).
    * To return from signal handler, or to start new thread in clone, we need to get consistent RIP
    * to the next instruction. The ioctl() with immediate_exit doesn't execute any guest code but
    * sets the registers, advancing RIP to the right location.
    */
   vcpu->cpu_run->immediate_exit = 1;
   (void)ioctl(vcpu->kvm_vcpu_fd, KVM_RUN, NULL);
   errno = 0;   // reset EINTR from ioctl above
   vcpu->cpu_run->immediate_exit = 0;
}

extern FILE* km_log_file;

void __km_trace(int errnum, const char* function, int linenumber, const char* fmt, ...)
    __attribute__((__format__(__printf__, 4, 5)));
void km_trace_include_pid(uint8_t trace_pid);
uint8_t km_trace_include_pid_value(void);
void km_trace_set_noninteractive(void);
void km_trace_set_log_file_name(char* kmlog_file_name);

char* km_get_self_name(void);

// Interrupt handling.
void km_init_guest_idt(void);
void km_handle_interrupt(km_vcpu_t* vcpu);

// After km start, used to signal VCPU thread to force KVM exit
static const int KM_SIGVCPUSTOP = (__SIGRTMAX - 1);

/*
 * To check for success/failure from plain system calls and similar logic, returns -1 and sets
 * errno if fail.
 */
static inline long km_syscall_ok(uint64_t r)
{
   if (r > -4096ul) {
      errno = -r;
      return -1;
   }
   return r;
}

static const struct timespec _1ms = {
    .tv_sec = 0, .tv_nsec = 1000000, /* 1 millisec */
};

/*
 * Trivial trace control - with switch to turn on/off and on and a tag to match.
 * E.g. "-Vgdb" will only match GDB related messages, and '-V(gdb|kvm)' will match both gdb and
 * kvm-related messages. The "gdb" "kvm" tag is passed to km_info API
 */
typedef struct km_info_trace {
   regex_t tags;   // only trace the tags matching this regexp
   enum {
      KM_TRACE_NONE,
      KM_TRACE_TAG,
      KM_TRACE_INFO,
      KM_TRACE_WARN,
      KM_TRACE_ERR
   } level;   // trace level. using only KM_TRACE_NONE for now
} km_info_trace_t;
extern km_info_trace_t km_info_trace;
extern char* km_payload_name;

void km_trace_setup(int argc, char* argv[]);

extern int km_collect_hc_stats;

#define km_trace_enabled() (km_info_trace.level != KM_TRACE_NONE)      // 1 for yes, 0 for no
#define km_trace_enabled_tag() (km_info_trace.level == KM_TRACE_TAG)   // 1 for yes, 0 for no

#define km_trace_tag_enabled(tag)                                                                  \
   (km_trace_enabled() &&                                                                          \
    (km_trace_enabled_tag() == 0 || regexec(&km_info_trace.tags, tag, 0, NULL, 0) == 0))

// Trace something if tag matches, and add perror() output to end of the line.
#define km_info(tag, fmt, ...)                                                                     \
   do {                                                                                            \
      if (km_trace_tag_enabled(tag) != 0)                                                          \
         __km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                            \
   } while (0)

// Trace something to stderr but don't include perror() output.
#define km_infox(tag, fmt, ...)                                                                    \
   do {                                                                                            \
      if (km_trace_tag_enabled(tag) != 0)                                                          \
         __km_trace(0, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                \
   } while (0)

// trace no matter what the tag is, but only if -V is enabled
#define km_trace(fmt, ...)                                                                         \
   do {                                                                                            \
      if (km_trace_enabled() != 0)                                                                 \
         __km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                            \
   } while (0)

#define km_tracex(fmt, ...)                                                                        \
   do {                                                                                            \
      if (km_trace_enabled() != 0)                                                                 \
         __km_trace(0, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                \
   } while (0)

#define km_errx(exit_status, fmt, ...)                                                             \
   do {                                                                                            \
      __km_trace(0, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                   \
      exit(exit_status);                                                                           \
   } while (0)

#define km_err(exit_status, fmt, ...)                                                              \
   do {                                                                                            \
      __km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                               \
      exit(exit_status);                                                                           \
   } while (0)

#define km_warnx(fmt, ...)                                                                         \
   do {                                                                                            \
      __km_trace(0, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                   \
   } while (0)

#define km_warn(fmt, ...)                                                                          \
   do {                                                                                            \
      __km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                               \
   } while (0)

#define km_mutex_lock(mutex)                                                                       \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_mutex_lock(mutex)) != 0) {                                                \
         km_err(ret, "pthread_mutex_lock(" #mutex ") Failed");                                     \
      }                                                                                            \
   } while (0)

#define km_mutex_unlock(mutex)                                                                     \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_mutex_unlock(mutex)) != 0) {                                              \
         km_err(ret, "pthread_mutex_unlock(" #mutex ") Failed");                                   \
      }                                                                                            \
   } while (0)

#define km_cond_broadcast(cond)                                                                    \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_broadcast(cond)) != 0) {                                             \
         km_err(ret, "pthread_cond_broadcast(" #cond ") Failed");                                  \
      }                                                                                            \
   } while (0)

#define km_cond_signal(cond)                                                                       \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_signal(cond)) != 0) {                                                \
         km_err(ret, "pthread_cond_signal(" #cond ") Failed");                                     \
      }                                                                                            \
   } while (0)

#define km_cond_wait(cond, mutex)                                                                  \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_wait(cond, mutex)) != 0) {                                           \
         km_err(ret, "pthread_cond_wait(" #cond ", " #mutex ") Failed");                           \
      }                                                                                            \
   } while (0)

static inline int
km_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, struct timespec* abstime)
{
   int ret;
   if ((ret = pthread_cond_timedwait(cond, mutex, abstime)) != 0) {
      if (ret != ETIMEDOUT) {
         km_err(ret, "pthread_cond_timedwait(cond %p) failed, %s", cond, strerror(ret));
      }
   }
   return ret;
}

#define km_getname_np(target_thread, threadname, buflen)                                             \
   do {                                                                                              \
      int ret;                                                                                       \
      if ((ret = pthread_getname_np(target_thread, threadname, buflen)) != 0) {                      \
         km_err(ret, "pthread_getname_np(" #target_thread ", " #threadname ", " #buflen ") Failed"); \
      }                                                                                              \
   } while (0)

#define km_setname_np(target_thread, name)                                                         \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_setname_np(target_thread, name)) != 0) {                                  \
         km_err(ret, "pthread_setname_np(" #target_thread ", " #name ") Failed");                  \
      }                                                                                            \
   } while (0)

#define km_attr_setstacksize(attr, stacksize)                                                      \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_setstacksize(attr, stacksize)) != 0) {                               \
         km_err(ret, "pthread_attr_setstacksize(" #attr ", " #stacksize ") Failed");               \
      }                                                                                            \
   } while (0)

#define km_attr_init(attr)                                                                         \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_init(attr)) != 0) {                                                  \
         km_err(ret, "pthread_attr_init(" #attr ") Failed");                                       \
      }                                                                                            \
   } while (0)

#define km_attr_destroy(attr)                                                                      \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_destroy(attr)) != 0) {                                               \
         km_err(ret, "pthread_attr_destroy(" #attr ") Failed");                                    \
      }                                                                                            \
   } while (0)

#define km_sigmask(how, set, oldset)                                                               \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_sigmask(how, set, oldset)) != 0) {                                        \
         km_err(ret, "pthread_sigmask(" #how ", " #set ", " #oldset ") Failed");                   \
      }                                                                                            \
   } while (0)

#define km_pkill(vcpu, signo)                                                                      \
   do {                                                                                            \
      int ret;                                                                                     \
      sigval_t val = {.sival_ptr = vcpu};                                                          \
      if ((ret = pthread_sigqueue(vcpu->vcpu_thread, signo, val)) != 0) {                          \
         km_err(ret, "pthread_sigqueue(" #vcpu "->vcpu_thread, " #signo ") Failed ");              \
      }                                                                                            \
   } while (0)

static inline int km_vcpu_run_cnt(void)
{
   int ret;

   km_mutex_lock(&machine.vm_vcpu_mtx);
   ret = machine.vm_vcpu_run_cnt;
   km_mutex_unlock(&machine.vm_vcpu_mtx);
   return ret;
}

static inline void km_lock_vcpu_thr(km_vcpu_t* vcpu)
{
   km_mutex_lock(&vcpu->thr_mtx);
}

static inline void km_unlock_vcpu_thr(km_vcpu_t* vcpu)
{
   km_mutex_unlock(&vcpu->thr_mtx);
}

static inline void km_mem_lock(void)
{
   km_mutex_lock(&machine.brk_mutex);
}

static inline void km_mem_unlock(void)
{
   km_mutex_unlock(&machine.brk_mutex);
}

static inline void km_signal_lock(void)
{
   km_mutex_lock(&machine.signal_mutex);
}

static inline void km_signal_unlock(void)
{
   km_mutex_unlock(&machine.signal_mutex);
}

// tags for different traces
#define KM_TRACE_VCPU "vcpu"
#define KM_TRACE_KVM "kvm"
#define KM_TRACE_MEM "mem"
#define KM_TRACE_MMAP "mmap"
#define KM_TRACE_COREDUMP "coredump"
#define KM_TRACE_SIGNALS "signals"
#define KM_TRACE_DECODE "decode"
#define KM_TRACE_PROC "proc"
#define KM_TRACE_EXEC "exec"
#define KM_TRACE_FORK "fork"   // also clone() for a process.
#define KM_TRACE_ARGS "args"

/*
 * The km definition of the link_map structure in runtime/musl/include/link.h
 */
typedef struct km_link_map {
   uint64_t l_addr;
   char* l_name;
   uint64_t l_ld;
   struct km_link_map* l_next;
   struct km_link_map* l_prev;
} link_map_t;

// Helper function to visit entries in the dynamically loaded modules list.
typedef int(link_map_visit_function_t)(link_map_t* kma, link_map_t* gva, void* visitargp);

int km_link_map_walk(link_map_visit_function_t* callme, void* visitargp);

char* km_traverse_payload_symlinks(const char* name);
char* km_parse_shebang(const char* payload_file, char** extra_arg);

// km_decode.c
void* km_find_faulting_address(km_vcpu_t* vcpu);
void km_x86decode(km_vcpu_t* vcpu);

// km_vmdriver.c
int km_vmdriver_get_identity(void);
void km_vmdriver_machine_init(void);
void km_vmdriver_vcpu_init(km_vcpu_t* vcpu);
int km_vmdriver_lowest_kernel();
size_t km_vmdriver_fpstate_size();
int km_vmdriver_save_fpstate(km_vcpu_t* vcpu, void* addr, int fptype, int preserve_state);
int km_vmdriver_restore_fpstate(km_vcpu_t* vcpu, void* addr, int fptype);
void km_vmdriver_clone(km_vcpu_t* vcpu, km_vcpu_t* new_vcpu);
void km_vmdriver_save_fork_info(km_vcpu_t* vcpu, uint8_t* ksi_valid, void* ksi);
void km_vmdriver_restore_fork_info(km_vcpu_t* vcpu, uint8_t ksi_valid, void* ksi);
int km_vmdriver_fp_format(km_vcpu_t* vcpu);

#endif /* #ifndef __KM_H__ */
