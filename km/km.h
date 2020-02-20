/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
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
#include <stdlib.h>
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
 * When a thread hits a breakpoint, or a single step operation completes, or
 * a signal is generated, a gdb event is queued by the thread and then the
 * gdb server is woken.  The gdb server then processes the elements in the
 * queue.  This structure describes a thread's gdb event.
 */
typedef struct gdb_event {
   TAILQ_ENTRY(gdb_event) link;   // link field for being in gdb's event queue
   uint8_t entry_is_active;       // to prevent us from overwriting a thread's single gdb_event
   pid_t sigthreadid;             // the thread this entry belongs to
   int signo;                     // the reason a thread has generated a gdb event
   int exit_reason;               // the reason from kvm for exiting the guest vcpu
} gdb_event_t;

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

/*
 * VPCU state transition:
 * unused/unallocated -> used: .is_used == 1 km_vcpu_get()
 * used -> active: .is_active == 1 km_run_vcpu_thread()
 * in and out of the guest -> .is_running == 1 km_vcpu_one_kvm_run()
 * and back to unused in km_vcpu_stopped()
 */
typedef struct km_vcpu {
   int vcpu_id;               // uniq ID
   int kvm_vcpu_fd;           // this VCPU file descriptor
   kvm_run_t* cpu_run;        // run control region
   pthread_t vcpu_thread;     // km pthread
   pthread_mutex_t thr_mtx;   // protects the three fields below
   pthread_cond_t thr_cv;     // used by vcpu_pthread to block while vcpu isn't in use
   uint8_t is_used;           // 1 means slot is taken, 0 means 'ready for reuse'
   uint8_t is_active;         // 1 VCPU thread is running, 0 means it is "parked" or not started yet
   uint8_t is_running;        // 1 means the vcpu is in guest, aka ioctl (KVM_RUN)
   uint8_t regs_valid;        // Are registers valid?
   uint8_t sregs_valid;       // Are segment registers valid?
                              //
   km_gva_t stack_top;        // also available in guest_thr
   km_gva_t guest_thr;        // guest pthread, FS reg in the guest
                              //
   kvm_regs_t regs;           // Cached register values.
   kvm_sregs_t sregs;         // Cached segment register values.
   km_sigset_t sigmask;       // blocked signals for thread
   km_signal_list_t sigpending;   // List of signals sent to thread
   /*
    * Linux/Pthread handshake hacks. These are actually part of the standard.
    */
   km_gva_t set_child_tid;     // See 'man 2 set_child_tid' for details
   km_gva_t clear_child_tid;   // See 'man 2 set_child_tid' for details

   uint64_t dr_regs[4];   // remember the addresses we are watching and have written into
                          // the processor's debugging facilities in DR0 - DR3.
   gdb_vcpu_state_t gdb_vcpu_state;   // gdb's per thread (vcpu) state.
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
                                        // Note: if too much of it is accessed, we expect Linux
                                        // OOM killer to kick in
} km_machine_init_params_t;

void km_machine_init(km_machine_init_params_t* params);
void km_signal_machine_fini(void);
void km_machine_fini(void);
void* km_vcpu_run_main(km_vcpu_t* unused);
void* km_vcpu_run(km_vcpu_t* vcpu);
int km_run_vcpu_thread(km_vcpu_t* vcpu, void* run(km_vcpu_t*));
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

#define KM_MEM_SLOTS 42   // We use 36 on 512GB machine, 42 on 4TB, out of 509 KVM_USER_MEM_SLOTS
                          // slot 0 is used for pages tables and some other things
                          // slot 41 is used to map the vdso and vvar pages into the payload address
                          //  space

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
   km_filesys_t filesys;
   km_mmap_cb_t mmaps;   // guest memory regions managed with mmaps/mprotect/munmap
   void* auxv;           // Copy of process AUXV (used if core is dumped)
   size_t auxv_size;     // size of process AUXV (used if core is dumped)
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
void km_exit(km_vcpu_t* vcpu);

void km_vcpu_stopped(km_vcpu_t* vcpu);
km_vcpu_t* km_vcpu_get(void);
void km_vcpu_put(km_vcpu_t* vcpu);
int km_vcpu_set_to_run(km_vcpu_t* vcpu, km_gva_t start, uint64_t arg1, uint64_t arg2);
void km_vcpu_detach(km_vcpu_t* vcpu);

typedef int (*km_vcpu_apply_cb)(km_vcpu_t* vcpu, uint64_t data);   // return 0 if all is good
extern int km_vcpu_apply_used(km_vcpu_apply_cb func, uint64_t data);
extern int km_vcpu_apply_all(km_vcpu_apply_cb func, uint64_t data);
extern int km_vcpu_count(void);
extern void km_vcpu_pause_all(void);
extern km_vcpu_t* km_vcpu_fetch_by_tid(int tid);

extern void km_trace(int errnum, const char* function, int linenumber, const char* fmt, ...)
    __attribute__((__format__(__printf__, 4, 5)));

// Interrupt handling.
void km_init_guest_idt(km_gva_t handlers);
void km_handle_interrupt(km_vcpu_t* vcpu);

#define KM_SIGVCPUSTOP SIGUSR1   //  After km start, used to signal VCP thread to force KVM exit

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
      KM_TRACE_INFO,
      KM_TRACE_WARN,
      KM_TRACE_ERR
   } level;   // trace level. using only KM_TRACE_NONE for now
} km_info_trace_t;
extern km_info_trace_t km_info_trace;

#define km_trace_enabled() (km_info_trace.level != KM_TRACE_NONE)   // 1 for yes, 0 for no

#define km_trace_tag_enabled(tag)                                                                  \
   (km_trace_enabled() && regexec(&km_info_trace.tags, tag, 0, NULL, 0) == 0)
/*
 * Trace something and add perror() output to end of the line.
 */
#define km_info(tag, fmt, ...)                                                                     \
   do {                                                                                            \
      if (km_trace_tag_enabled(tag) != 0)                                                          \
         km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                              \
   } while (0)

/*
 * Trace something to stderr but don't include perror() output.
 */
#define km_infox(tag, fmt, ...)                                                                    \
   do {                                                                                            \
      if (km_trace_tag_enabled(tag) != 0)                                                          \
         km_trace(0, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                  \
   } while (0)

#define km_err_msg(errno, fmt, ...)                                                                \
   do {                                                                                            \
      km_trace(errno, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);                                 \
   } while (0)

#define km_mutex_lock(mutex)                                                                       \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_mutex_lock(mutex)) != 0) {                                                \
         km_err_msg(ret, "pthread_mutex_lock(" #mutex ") Failed");                                 \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_mutex_unlock(mutex)                                                                     \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_mutex_unlock(mutex)) != 0) {                                              \
         km_err_msg(ret, "pthread_mutex_unlock(" #mutex ") Failed");                               \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_cond_broadcast(cond)                                                                    \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_broadcast(cond)) != 0) {                                             \
         km_err_msg(ret, "pthread_cond_broadcast(" #cond ") Failed");                              \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_cond_signal(cond)                                                                       \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_signal(cond)) != 0) {                                                \
         km_err_msg(ret, "pthread_cond_signal(" #cond ") Failed");                                 \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_cond_wait(cond, mutex)                                                                  \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_cond_wait(cond, mutex)) != 0) {                                           \
         km_err_msg(ret, "pthread_cond_wait(" #cond ", " #mutex ") Failed");                       \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_getname_np(target_thread, threadname, buflen)                                                 \
   do {                                                                                                  \
      int ret;                                                                                           \
      if ((ret = pthread_getname_np(target_thread, threadname, buflen)) != 0) {                          \
         km_err_msg(ret, "pthread_getname_np(" #target_thread ", " #threadname ", " #buflen ") Failed"); \
         abort();                                                                                        \
      }                                                                                                  \
   } while (0)

#define km_setname_np(target_thread, name)                                                         \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_setname_np(target_thread, name)) != 0) {                                  \
         km_err_msg(ret, "pthread_setname_np(" #target_thread ", " #name ") Failed");              \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_attr_setstacksize(attr, stacksize)                                                      \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_setstacksize(attr, stacksize)) != 0) {                               \
         km_err_msg(ret, "pthread_attr_setstacksize(" #attr ", " #stacksize ") Failed");           \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_attr_init(attr)                                                                         \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_init(attr)) != 0) {                                                  \
         km_err_msg(ret, "pthread_attr_init(" #attr ") Failed");                                   \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_attr_destroy(attr)                                                                      \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_attr_destroy(attr)) != 0) {                                               \
         km_err_msg(ret, "pthread_attr_destroy(" #attr ") Failed");                                \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_sigmask(how, set, oldset)                                                               \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_sigmask(how, set, oldset)) != 0) {                                        \
         km_err_msg(ret, "pthread_sigmask(" #how ", " #set ", " #oldset ") Failed");               \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

#define km_pkill(threadid, signo)                                                                  \
   do {                                                                                            \
      int ret;                                                                                     \
      if ((ret = pthread_kill(threadid, signo)) != 0) {                                            \
         km_err_msg(ret, "pthread_kill(" #threadid ", " #signo ") Failed ");                       \
         abort();                                                                                  \
      }                                                                                            \
   } while (0)

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

extern int km_link_map_walk(link_map_visit_function_t* callme, void* visitargp);

// km_decode.c
void* km_find_faulting_address(km_vcpu_t* vcpu);
void km_x86decode(km_vcpu_t* vcpu);

#endif /* #ifndef __KM_H__ */
