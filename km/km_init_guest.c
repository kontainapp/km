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
km_tls_module_t km_main_tls;

typedef struct km_builtin_tls {
   char c;
   km_pthread_t pt;
   void* space[16];
} km_builtin_tls_t;
#define MIN_TLS_ALIGN offsetof(km_builtin_tls_t, pt)

/*
 * km_load_elf() finds __libc by name in the ELF image of the guest. We follow __init_libc() logic
 * to initialize the content, including TLS and pthread structure for the main thread. TLS is
 * allocated on top of the stack. pthread is part of TLS area.
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
 * TLS paper above explain TLS on various platforms and in generic way. We implement here
 * simplification.
 *
 * One simplification is that being one static executable we only have one tls module, instead of
 * linked list of nmodules, one per dynamically linked/loaded object. Correspondingly the dtv
 * contains only one pointer, dtv[1], and generation number (dtv[0]) never changes.
 * Another simplification is that for now we always put TLS on the stack, thus we allocate stack to
 * contain requested stack size and TLS.
 * In addition to TLS there is also TSD (pthread_key_create/pthread_setspecific and such). For the
 * main thread TSD is placed in a separate area, which gets linked in the guest code if the facility
 * is used. Note pthread_create() assignment of tsd field. For subsequent threads we put TSD also on
 * the stack.
 *
 * For the main thread, going from the top of the allocated stack area down, we place Thread Control
 * Block (TCB, km_pthread_t), then TLS, then dtv. All with the appropriate alignment derived either
 * from structure packing (MIN_TLS_ALIGN) or coming from TLS extend in ELF. Below dtv for main
 * thread we put dummy (empty) AUXV and environ, the argv built from the args, and then goes the
 * initial top of the stack.
 *
 * For the subsequent threads on the very top goes fixed size TSD area. Below that goes TCB, then
 * TLS, then dtv, and the initial top of the stack, again all appropriately alligned.
 *
 * TODO: There are other guest structures, such as __hwcap, __sysinfo, __progname and so
 * on, we will need to process them as well most likely.
 */
void km_init_libc_main(km_vcpu_t* vcpu, int argc, char* const argv[])
{
   km_gva_t libc = km_guest.km_libc;
   km__libc_t* libc_kma = NULL;
   km_gva_t tcb;
   km_pthread_t* tcb_kma;
   uintptr_t* dtv_kma;
   km_gva_t dtv;
   km_gva_t map_base;

   if (km_guest.km_handlers == 0) {
      errx(1, "Bad binary - cannot find interrupt handler");
   }
   km_init_guest_idt(km_guest.km_handlers);
   if (km_syscall_ok(map_base = km_guest_mmap_simple(GUEST_STACK_SIZE)) < 0) {
      err(1, "Failed to allocate memory for main stack");
   }
   tcb = rounddown(map_base + GUEST_STACK_SIZE - sizeof(km_pthread_t), MIN_TLS_ALIGN);

   if (libc != 0) {
      libc_kma = km_gva_to_kma(libc);
      libc_kma->auxv = NULL;   // for now
      libc_kma->page_size = KM_PAGE_SIZE;
      libc_kma->secure = 1;
      libc_kma->can_do_threads = 0;   // Doesn't seem to matter either way
   }
   /*
    * km_main_tls is initialized from ELF PT_TLS header. dtv is part of TLS: dtv[0] is generation #,
    * dtv[1] is a pointer to the only TLS area as this is static program.
    *
    * Normally km_main_tls is in executable memory, and is used to initialize newly created threads'
    * TLS. In our case that logic is all in KM, so we keep that info in KM as well, and only
    * initialize part of libc used by runtime TLS, or __tls_get_addr(), which means for example
    * libc_kma->tls_{size,head,cnt} are NULL as not used.
    */
   km_main_tls.align = MAX(km_main_tls.align, MIN_TLS_ALIGN);
   km_gva_t tls = rounddown(tcb - km_main_tls.size, km_main_tls.align);
   dtv = rounddown(tls - 2 * sizeof(void*), sizeof(void*));
   dtv_kma = km_gva_to_kma(dtv);
   dtv_kma[0] = 1;   // static executable
   dtv_kma[1] = tls;
   km_main_tls.offset = tcb - tls;
   if (km_main_tls.len != 0) {
      memcpy(km_gva_to_kma(tls), km_main_tls.image, km_main_tls.len);
   }

   tcb_kma = km_gva_to_kma(tcb);
   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)dtv;
   tcb_kma->locale = libc != 0 ? &((km__libc_t*)libc)->global_locale : NULL;
   tcb_kma->map_base = (typeof(tcb_kma->map_base))map_base;
   tcb_kma->map_size = GUEST_STACK_SIZE;
   tcb_kma->self = (km_pthread_t*)tcb;
   tcb_kma->detach_state = DT_JOINABLE;
   tcb_kma->robust_list.head = &((km_pthread_t*)tcb)->robust_list.head;

   char* argv_km[argc + 1];   // argv to copy to guest stack_init
   km_gva_t stack_top = dtv;
   km_kma_t stack_top_kma = km_gva_to_kma(stack_top);

   argv_km[argc] = NULL;
   for (argc--; argc >= 0; argc--) {
      int len = strnlen(argv[argc], PATH_MAX) + 1;

      stack_top -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         errx(2, "Argument list is too large");
      }
      argv_km[argc] = (char*)stack_top;
      stack_top_kma -= len;
      strncpy(stack_top_kma, argv[argc], len);
   }
   stack_top = rounddown(stack_top, sizeof(void*));
   stack_top_kma = km_gva_to_kma(stack_top);
   static const int size_of_empty_aux_and_env = 4 * sizeof(void*);
   stack_top -= size_of_empty_aux_and_env;
   stack_top_kma -= size_of_empty_aux_and_env;
   memset(stack_top_kma, 0, size_of_empty_aux_and_env);
   *(char**)stack_top_kma = (char*)(stack_top + sizeof(char*));

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
   km_pthread_t* tcb_kma;
   km_gva_t tcb;
   km_gva_t tsd;
   km_gva_t map_base;
   size_t map_size;
   size_t tsd_size;
   uintptr_t* dtv_kma;
   km_gva_t dtv;

   if (km_guest.km_tsd_size == 0) {
      errx(1, "Bad binary - cannot find __pthread_tsd_size");
   }
   tsd_size = *(size_t*)km_gva_to_kma(km_guest.km_tsd_size);
   assert(tsd_size <= sizeof(void*) * PTHREAD_KEYS_MAX &&
          tsd_size == rounddown(tsd_size, sizeof(void*)));

   assert(_a_stackaddr(g_attr) == 0);   // TODO: for now
   assert(libc != 0);

   map_size = _a_stacksize(g_attr) == 0
                  ? DEFAULT_STACK_SIZE
                  : roundup(_a_stacksize(g_attr) + km_main_tls.size + sizeof(km_pthread_t) + tsd_size,
                            KM_PAGE_SIZE);
   if (km_syscall_ok(map_base = km_guest_mmap_simple(map_size)) < 0) {
      return 0;
   }
   tsd = map_base + map_size - tsd_size;   // aligned because mmap and assert tsd_size above
   memset(km_gva_to_kma(tsd), 0, tsd_size);
   tcb = rounddown(tsd - sizeof(km_pthread_t), MIN_TLS_ALIGN);
   tcb_kma = km_gva_to_kma(tcb);
   memset(tcb_kma, 0, sizeof(km_pthread_t));

   km_gva_t tls = rounddown(tcb - km_main_tls.size, km_main_tls.align);
   dtv = rounddown(tls - 2 * sizeof(void*), sizeof(void*));
   dtv_kma = km_gva_to_kma(dtv);
   dtv_kma[0] = 1;   // static executable
   dtv_kma[1] = tls;
   if (km_main_tls.len != 0) {
      memcpy(km_gva_to_kma(tls), km_main_tls.image, km_main_tls.len);
   }

   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)dtv;
   tcb_kma->stack = (typeof(tcb_kma->stack))dtv;
   tcb_kma->stack_size = (typeof(tcb_kma->stack_size))tcb_kma->stack - map_base;
   tcb_kma->map_base = (typeof(tcb_kma->map_base))map_base;
   tcb_kma->map_size = map_size;
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
                      pthread_tid_t* restrict pid,
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
   vcpu->sigmask = current_vcpu->sigmask;
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
      *pid = vcpu->guest_thr;
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
      vcpu = km_vcpu_fetch(vcpu->joining_pid);
      if (vcpu != NULL && vcpu->joining_pid == current_vcpu->vcpu_id) {
         return -EDEADLOCK;
      }
   }
   return 0;
}

int km_pthread_join(km_vcpu_t* current_vcpu, pthread_tid_t pid, km_kma_t ret)
{
   km_vcpu_t* vcpu;
   int rc = 0;

   if ((vcpu = km_vcpu_fetch(pid)) == NULL) {
      return -ESRCH;
   }
   km_pthread_t* pt_kma = km_gva_to_kma(vcpu->guest_thr);
   assert(pt_kma != NULL);
   if (pt_kma->detach_state == DT_DETACHED || pt_kma->detach_state == DT_DYNAMIC) {
      return -EINVAL;
   }
   if ((rc = check_join_deadlock(vcpu, current_vcpu)) != 0) {
      return rc;
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
   current_vcpu->joining_pid = -1;
   if (rc == 0) {
      km_vcpu_put(vcpu);
   }
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
