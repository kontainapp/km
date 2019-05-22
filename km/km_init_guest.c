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
 *
 * After the guest is loaded on memory, initialize it's execution environment.
 * This includes passing parameters to main, but more importantly setting values in
 * struct __libc and friends.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>   // getpid() has no libc wrapper, so need to do syscall

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"

/*
 * Definitions and structures from varios musl files
 */
#define LOCALE_NAME_MAX 23

typedef struct km__locale_map {
   const void* map;
   size_t map_size;
   char name[LOCALE_NAME_MAX + 1];
   const struct km__locale_map* next;
} km__locale_map_t;

typedef struct km__locale_struct {
   const struct km__locale_map* cat[6];
} km__locale_t;

typedef struct km_tls_module {
   struct km_tls_module* next;
   void* image;
   size_t len, size, align, offset;
} km_tls_module_t;

typedef struct km__libc {
   int can_do_threads;
   int threaded;
   int secure;
   volatile int threads_minus_1;
   size_t* auxv;
   struct km_tls_module* tls_head;
   size_t tls_size, tls_align, tls_cnt;
   size_t page_size;
   km__locale_t global_locale;
} km__libc_t;

typedef struct km__ptcb {
   void (*__f)(void*);
   void* __x;
   struct __ptcb* __next;
} km__ptcb_t;

enum {
   DT_EXITED = 0,
   DT_EXITING,
   DT_JOINABLE,
   DT_DETACHED,
   DT_DYNAMIC,   // started as JOINABLE then called detach()
};

typedef struct km_pthread {
   /* Part 1 -- these fields may be external or
    * internal (accessed via asm) ABI. Do not change. */
   struct km_pthread* self;
   uintptr_t* dtv;
   void *unused1, *unused2;
   uintptr_t sysinfo;
   uintptr_t canary, canary2;

   /* Part 2 -- implementation details, non-ABI. */
   int tid;
   int errno_val;
   volatile int detach_state;
   volatile int cancel;
   volatile unsigned char canceldisable, cancelasync;
   unsigned char tsd_used : 1;
   unsigned char unblock_cancel : 1;
   unsigned char dlerror_flag : 1;
   unsigned char* map_base;
   size_t map_size;
   void* stack;
   size_t stack_size;
   size_t guard_size;
   void* start_arg;
   void* (*start)(void*);
   void* result;
   km__ptcb_t* cancelbuf;
   void** tsd;
   struct {
      volatile void* volatile head;
      long off;
      volatile void* volatile pending;
   } robust_list;
   volatile int timer_id;
   km__locale_t* locale;
   volatile int killlock[1];
   char* dlerror_buf;
   void* stdio_locks;

   /* Part 3 -- the positions of these fields relative to
    * the end of the structure is external and internal ABI. */
   uintptr_t canary_at_end;
   uintptr_t* dtv_copy;
} km_pthread_t;

/*
 * Normally this structure is part of the guest payload, however its use is very specific. It keeps
 * the description of the TLS memory and used for as a source of initialization for new threads TLS.
 * So we keep it in the monitor.
 */
struct km_tls_module km_main_tls;

typedef struct km_builtin_tls {
   char c;
   km_pthread_t pt;
   void* space[16];
} km_builtin_tls_t;
#define MIN_TLS_ALIGN offsetof(km_builtin_tls_t, pt)

km_builtin_tls_t builtin_tls[1];

/*
 * km_load_elf() finds __libc by name in the ELF image of the guest. We follow __init_libc() logic to
 * initialize the content, including TLS and pthread structure for the main thread. TLS is allocated
 * on top of the stack. pthread is part of TLS area.
 *
 * Care needs to be taken to distinguish between addresses seen in the guest and in km. We have two
 * sets of variables for each structure we deal with, like libc and libc_kma, former and latter
 * correspondingly. The guest addresses are, as everywhere in km, of km_gva_t (aka uint64_t). km
 * addresses are of the type there are in the guest. When we need to obtain addresses of subfields
 * in the guests we cast the km_gva_t to the appropriate pointer, then use &(struct)->field.
 *
 * Also we distingush two case - the one when libc is part of executable (the most commin case), and
 * when it isn't. The latter assumes some very simple payload, or something incompatible with libc.
 *
 * See TLS description as referenced in gcc manual https://gcc.gnu.org/onlinedocs/gcc-8.2.0/gcc.pdf
 * in section 6.63 Thread-Local Storage, at the time of this writing pointing at
 * https://www.akkadia.org/drepper/tls.pdf.
 *
 * TODO: There are other guest structures, such as __environ, __hwcap, __sysinfo, __progname and so
 * on, we will need to process them as well most likely.
 */
void km_init_libc_main(km_vcpu_t* vcpu, int argc, char* const argv[])
{
   km_gva_t libc = km_guest.km_libc;
   km_gva_t handlers = km_guest.km_handlers;
   km__libc_t* libc_kma = NULL;
   km_pthread_t* tcb_kma;
   km_gva_t tcb;
   km_gva_t map_base;
   km_gva_t stack_top;

   if (km_syscall_ok(map_base = km_guest_mmap_simple(GUEST_STACK_SIZE)) < 0) {
      err(1, "Failed to allocate memory for main stack");
   }
   stack_top = map_base + GUEST_STACK_SIZE;

   if (libc != 0) {
      libc_kma = km_gva_to_kma(libc);
      libc_kma->auxv = NULL;   // for now
      libc_kma->page_size = KM_PAGE_SIZE;
      libc_kma->secure = 1;
      libc_kma->can_do_threads = 0;   // Doesn't seem to matter either way
      /*
       * TODO: km_main_tls should be initialized from ELF headers of PT_TLS, PT_PHDR ... type to get
       * information about guest program specific TLS. For now we go with minimal TLS just to
       * support pthreads and internal data. As such, there is no need for the "2 * sizeof(void*)".
       * That space should be used for dtv which is part of TLS support. dtv[0] is generation #,
       * dtv[1] is a pointer to the only TLS area as this is static program.
       */
      libc_kma->tls_align = MIN_TLS_ALIGN;
      libc_kma->tls_size = roundup(2 * sizeof(void*) + sizeof(km_pthread_t), libc_kma->tls_align);
      tcb = rounddown(stack_top - sizeof(km_pthread_t), libc_kma->tls_align);
      stack_top -= libc_kma->tls_size;
   } else {
      stack_top = tcb = rounddown(stack_top - sizeof(km_pthread_t), MIN_TLS_ALIGN);
   }
   if (handlers == 0) {
      errx(1, "Bad binary - cannot find interrupt handler");
   }
   km_init_guest_idt(handlers);
   tcb_kma = km_gva_to_kma(tcb);
   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)stack_top;
   tcb_kma->locale = libc != 0 ? &((km__libc_t*)libc)->global_locale : NULL;
   tcb_kma->map_base = (typeof(tcb_kma->map_base))map_base;
   tcb_kma->map_size = GUEST_STACK_SIZE;
   tcb_kma->self = (km_pthread_t*)tcb;
   tcb_kma->detach_state = DT_JOINABLE;
   tcb_kma->robust_list.head = &((km_pthread_t*)tcb)->robust_list.head;

   char* argv_km[argc + 1];   // argv to copy to guest stack
   km_kma_t stack_top_kma = km_gva_to_kma(stack_top);

   argv_km[argc] = NULL;
   for (argc--; argc >= 0; argc--) {
      int len = roundup(strnlen(argv[argc], PATH_MAX), sizeof(km_gva_t));

      stack_top -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         errx(2, "Argument list is too large");
      }
      argv_km[argc] = (char*)stack_top;
      stack_top_kma -= len;
      strncpy(stack_top_kma, argv[argc], len);
   }
   stack_top_kma -= sizeof(argv_km);
   stack_top -= sizeof(argv_km);
   memcpy(stack_top_kma, argv_km, sizeof(argv_km));

   tcb_kma->stack = (typeof(tcb_kma->stack))stack_top;
   tcb_kma->stack_size = stack_top - map_base;

   vcpu->guest_thr = tcb;
   vcpu->stack_top = stack_top;
}

#define DEFAULT_STACK_SIZE 131072
#define DEFAULT_GUARD_SIZE 8192

#define DEFAULT_STACK_MAX (8 << 20)
#define DEFAULT_GUARD_MAX (1 << 20)

typedef struct {
   union {
      int __i[14];
      volatile int __vi[14];
      unsigned long __s[7];
   } __u;
} km_pthread_attr_t;

static inline size_t _a_stacksize(const km_pthread_attr_t* restrict g_attr)
{
   return g_attr == NULL ? 0 : g_attr->__u.__s[0];
}
static inline size_t _a_guardsize(const km_pthread_attr_t* restrict g_attr)
{
   return g_attr == NULL ? 0 : g_attr->__u.__s[1];
}
static inline size_t _a_stackaddr(const km_pthread_attr_t* restrict g_attr)
{
   return g_attr == NULL ? 0 : g_attr->__u.__s[2];
}
#define __SU (sizeof(size_t) / sizeof(int))
static inline int _a_detach(const km_pthread_attr_t* restrict g_attr)
{
   return g_attr == NULL || g_attr->__u.__i[3 * __SU + 0] == 0 ? DT_JOINABLE : DT_DETACHED;
}

// #define _a_sched __u.__i[3 * __SU + 1]
// #define _a_policy __u.__i[3 * __SU + 2]
// #define _a_prio __u.__i[3 * __SU + 3]

/*
 * Allocate and initialize pthread structure for newly created thread in the guest.
 */
static km_gva_t
km_pthread_init(const km_pthread_attr_t* restrict g_attr, km_vcpu_t* vcpu, km_gva_t start, km_gva_t args)
{
   km_gva_t libc = km_guest.km_libc;
   km__libc_t* libc_kma;
   km_pthread_t* tcb_kma;
   km_gva_t tcb;
   km_gva_t tsd;
   km_gva_t map_base;
   size_t map_size;
   size_t tsd_size;

   if (km_guest.km_tsd_size == 0) {
      errx(1, "Bad binary - cannot find __pthread_tsd_size");
   }
   tsd_size = *(size_t*)km_gva_to_kma(km_guest.km_tsd_size);
   assert(tsd_size < sizeof(void*) * PTHREAD_KEYS_MAX);

   assert(_a_stackaddr(g_attr) == 0);   // TODO: for now
   assert(libc != 0);
   libc_kma = km_gva_to_kma(libc);

   map_size = _a_stacksize(g_attr) == 0
                  ? DEFAULT_STACK_SIZE
                  : roundup(_a_stacksize(g_attr) + libc_kma->tls_size + tsd_size, KM_PAGE_SIZE);
   if (km_syscall_ok(map_base = km_guest_mmap_simple(map_size)) < 0) {
      return 0;
   }
   tsd = map_base + map_size - tsd_size;
   tcb = rounddown(tsd - sizeof(km_pthread_t), libc_kma->tls_align);
   tcb_kma = km_gva_to_kma(tcb);
   memset(tcb_kma, 0, sizeof(km_pthread_t));
   tcb_kma->stack = (typeof(tcb_kma->stack))tsd - libc_kma->tls_size;
   tcb_kma->stack_size = (typeof(tcb_kma->stack_size))tcb_kma->stack - map_base;
   tcb_kma->map_base = (typeof(tcb_kma->map_base))map_base;
   tcb_kma->map_size = map_size;
   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)tcb_kma->stack;
   tcb_kma->self = (km_pthread_t*)tcb;
   tcb_kma->tsd = (typeof(tcb_kma->tsd))tsd;
   tcb_kma->detach_state = _a_detach(g_attr);
   tcb_kma->locale = &((km__libc_t*)libc)->global_locale;
   tcb_kma->robust_list.head = &((km_pthread_t*)tcb)->robust_list.head;
   tcb_kma->start = (void* (*)(void*))start;
   tcb_kma->start_arg = (void*)args;
   tcb_kma->tid = vcpu->vcpu_id;
   vcpu->guest_thr = tcb;
   vcpu->stack_top = (typeof(vcpu->stack_top))tcb_kma->stack;
   return tcb;
}

void km_pthread_fini(km_vcpu_t* vcpu)
{
   km_pthread_t* pt_kma = km_gva_to_kma(vcpu->guest_thr);

   vcpu->guest_thr = 0;
   vcpu->stack_top = 0;
   vcpu->is_paused = 0;
   if (pt_kma != NULL && pt_kma->map_base != NULL) {
      km_guest_munmap((km_gva_t)pt_kma->map_base, pt_kma->map_size);
   }
}

// glibc does not have a wrapper for SYS_gettid, let's add it
#ifdef SYS_gettid
pid_t gettid(void)
{
   return syscall(SYS_gettid);
}
#else
#error "SYS_gettid is not available"
#endif

static inline int km_run_vcpu_thread(km_vcpu_t* vcpu, const km_kma_t restrict attr)
{
   int rc;

   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
   vcpu->thr_state = VCPU_RUNNING;
   if (vcpu->vcpu_thread == 0) {
      pthread_attr_t vcpu_thr_att;

      pthread_attr_init(&vcpu_thr_att);
      pthread_attr_setstacksize(&vcpu_thr_att, 16 * KM_PAGE_SIZE);
      rc = -pthread_create(&vcpu->vcpu_thread, &vcpu_thr_att, (void* (*)(void*))km_vcpu_run, vcpu);
      pthread_attr_destroy(&vcpu_thr_att);
   } else {
      rc = -pthread_cond_signal(&vcpu->thr_cv);
   }
   if (rc != 0) {
      vcpu->thr_state = VCPU_IDLE;
      __atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt--
   }
   return rc;
}

// Create guest thread. We use vcpu->guest_thr for payload thread id.
int km_pthread_create(km_vcpu_t* current_vcpu,
                      pthread_t* restrict pid,
                      const km_kma_t restrict attr,
                      km_gva_t start,
                      km_gva_t args)
{
   km_gva_t pt;
   km_vcpu_t* vcpu;
   int rc;

   if (pid != NULL) {
      *pid = -1;
   }
   if ((vcpu = km_vcpu_get()) == NULL) {
      return -EAGAIN;
   }
   if ((pt = km_pthread_init(attr, vcpu, start, args)) == 0) {
      km_vcpu_put(vcpu);
      return -EAGAIN;
   }
   if ((rc = km_vcpu_set_to_run(vcpu, 0)) < 0) {
      km_vcpu_put(vcpu);
      return -EAGAIN;
   }
   if (km_run_vcpu_thread(vcpu, attr) < 0) {
      km_vcpu_put(vcpu);
      return -EAGAIN;
   }
   if (pid != NULL) {
      *pid = vcpu->vcpu_id;
   }
   return 0;
}

static int check_join_deadlock(km_vcpu_t* joinee_vcpu, km_vcpu_t* current_vcpu)
{
   km_vcpu_t* vcpu;

   if (current_vcpu == joinee_vcpu) {
      return -EDEADLOCK;
   }
   for (vcpu = joinee_vcpu; vcpu->joining_pid != -1;) {
      vcpu = machine.vm_vcpus[vcpu->joining_pid];
      assert(vcpu != NULL && vcpu->is_used != 0);
      if (vcpu->joining_pid == current_vcpu->vcpu_id) {
         return -EDEADLOCK;
      }
   }
   return 0;
}

int km_pthread_join(km_vcpu_t* current_vcpu, pthread_t pid, km_kma_t ret)
{
   km_vcpu_t* vcpu;
   int rc = 0;

   if (pid >= KVM_MAX_VCPUS || (vcpu = machine.vm_vcpus[pid]) == NULL || vcpu->is_used == 0) {
      return -ESRCH;
   }
   if ((rc = check_join_deadlock(vcpu, current_vcpu)) != 0) {
      return rc;
   }
   km_pthread_t* pt_kma = km_gva_to_kma(vcpu->guest_thr);
   assert(pt_kma != NULL);
   if (pt_kma->detach_state == DT_DETACHED || pt_kma->detach_state == DT_DYNAMIC) {
      return -EINVAL;
   }
   /*
    * Mark current thread as "joining someone else". pthread_cond_wait() is not interruptable,
    * so while in pthread_join the thread needs to be skipped when asking all vcpus to stop.
    * Note that join will complete naturally when the thread being joined exits due to signal.
    */
   current_vcpu->joining_pid = pid;
   /*
    * Note: there is no special theatment of main thread. 'man pthread_join' says:
    * "All of the threads in a process are peers: any thread can join with any other thread in
    * the process."
    */
   if (pthread_mutex_lock(&vcpu->thr_mtx) != 0) {
      err(1, "join: lock mutex thr_mtx");
   }
   switch (vcpu->thr_state) {
      case VCPU_RUNNING:
         vcpu->thr_state = VCPU_JOIN_WAITS;
         while (vcpu->thr_state != VCPU_DONE) {
            if (pthread_cond_wait(&vcpu->join_cv, &vcpu->thr_mtx) != 0) {
               err(1, "wait condition join_cv");
            }
         }
         /* fall through */
      case VCPU_DONE:
         if (ret != NULL) {
            *(int*)ret = vcpu->exit_status;
         }
         vcpu->thr_state = VCPU_IDLE;
         break;
      case VCPU_JOIN_WAITS:
         rc = -EINVAL;
         break;
      case VCPU_IDLE:
         rc = -ESRCH;
         break;
   }
   if (pthread_mutex_unlock(&vcpu->thr_mtx) != 0) {
      err(1, "join: unlock mutex thr_mtx");
   }
   if (rc == 0) {
      km_vcpu_put(vcpu);
   }
   current_vcpu->joining_pid = -1;
   return rc;
}

void km_vcpu_detach(km_vcpu_t* vcpu)
{
   km_pthread_t* pt_kma = km_gva_to_kma(vcpu->guest_thr);
   pt_kma->detach_state = DT_DETACHED;
}

void km_vcpu_stopped(km_vcpu_t* vcpu)
{
   km_pthread_t* pt_kma = km_gva_to_kma(vcpu->guest_thr);

   assert(pt_kma != NULL);
   if (pt_kma->detach_state == DT_DETACHED || pt_kma->detach_state == DT_DYNAMIC) {
      vcpu->thr_state = VCPU_IDLE;
      km_vcpu_put(vcpu);
   }

   // if (--machine.vm_vcpu_run_cnt == 0) {
   if (__atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST) == 0) {
      km_signal_machine_fini();
   }
   if (machine.exit_group == 1) {
      pthread_exit(NULL);
   }
   if (pthread_mutex_lock(&vcpu->thr_mtx) != 0) {
      err(1, "vcpu_stopped: lock mutex thr_mtx");
   }
   switch (vcpu->thr_state) {
      case VCPU_RUNNING:
         vcpu->thr_state = VCPU_DONE;
         break;

      case VCPU_JOIN_WAITS:
         vcpu->thr_state = VCPU_DONE;
         if (pthread_cond_signal(&vcpu->join_cv) != 0) {
            err(1, "signal condition join_cv");
         }
         break;

      default:
         break;
   }
   while (vcpu->thr_state != VCPU_RUNNING && vcpu->thr_state != VCPU_JOIN_WAITS) {
      if (pthread_cond_wait(&vcpu->thr_cv, &vcpu->thr_mtx) != 0) {
         err(1, "wait on condition thr_cv");
      }
   }
   if (pthread_mutex_unlock(&vcpu->thr_mtx) != 0) {
      err(1, "unlock mutex thr_mtx");
   }
}
