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
   DT_DYNAMIC,
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
 * load_elf() finds __libc by name in the ELF image of the guest. We follow __init_libc() logic to
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
      libc_kma->page_size = PAGE_SIZE;
      libc_kma->secure = 1;
      libc_kma->can_do_threads = 0;   // TODO: for now
      /*
       * TODO: km_main_tls should be initialized from ELF headers of PT_TLS, PT_PHDR ... type to get
       * information about guest program specific TLS. For now we go with minimal TLS just to
       * support pthreads and internal data. As such, there is no need for the "2 * sizeof(void*)".
       * That space should be used for dtv which is part of TLS support. dtv[0] is generation #,
       * dtv[1] is a pointer to the only TLS area as this is static program.
       */
      libc_kma->tls_align = MIN_TLS_ALIGN;
      libc_kma->tls_size = 2 * sizeof(void*) + sizeof(km_pthread_t) + MIN_TLS_ALIGN;
      tcb = rounddown(stack_top - sizeof(km_pthread_t), libc_kma->tls_align);
      stack_top -= libc_kma->tls_size;
   } else {
      stack_top = tcb = rounddown(stack_top - sizeof(km_pthread_t), MIN_TLS_ALIGN);
   }
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
   km_gva_t map_base;
   size_t map_size;

   assert(_a_stackaddr(g_attr) == 0);   // TODO: for now
   assert(libc != 0);
   libc_kma = km_gva_to_kma(libc);

   map_size = _a_stacksize(g_attr) == 0
                  ? DEFAULT_STACK_SIZE
                  : roundup(_a_stacksize(g_attr) + libc_kma->tls_size, PAGE_SIZE);
   if (km_syscall_ok(map_base = km_guest_mmap_simple(map_size)) < 0) {
      return 0;
   }
   tcb = rounddown(map_base + map_size - sizeof(km_pthread_t), libc_kma->tls_align);
   tcb_kma = km_gva_to_kma(tcb);
   memset(tcb_kma, 0, sizeof(km_pthread_t));
   tcb_kma->stack = (typeof(tcb_kma->stack))map_base + map_size - libc_kma->tls_size;
   tcb_kma->stack_size = map_size - libc_kma->tls_size;
   tcb_kma->map_base = (typeof(tcb_kma->map_base))map_base;
   tcb_kma->map_size = map_size;
   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)tcb_kma->stack;
   tcb_kma->self = (km_pthread_t*)tcb;
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
   vcpu->cpu_run->immediate_exit = 0;
   if (pt_kma != NULL && pt_kma->map_base != NULL) {
      km_guest_munmap((km_gva_t)pt_kma->map_base, pt_kma->map_size);
   }
}

int km_pthread_create(pthread_t* restrict pid, const km_kma_t restrict attr, km_gva_t start, km_gva_t args)
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

   pthread_attr_t vcpu_thr_att;

   pthread_attr_init(&vcpu_thr_att);
   if (_a_detach(attr) == DT_DETACHED) {
      pthread_attr_setdetachstate(&vcpu_thr_att, PTHREAD_CREATE_DETACHED);
   }
   pthread_attr_setstacksize(&vcpu_thr_att, 16 * PAGE_SIZE);
   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
   if (machine.pause_requested) {
      vcpu->cpu_run->immediate_exit = 1;
   }
   rc = -pthread_create(&vcpu->vcpu_thread, &vcpu_thr_att, (void* (*)(void*))km_vcpu_run, vcpu);
   pthread_attr_destroy(&vcpu_thr_att);
   if (rc != 0) {
      __atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt--
      km_vcpu_put(vcpu);
      return rc;
   }
   if (pid != NULL) {
      *pid = vcpu->vcpu_id;
   }
   return 0;
}

static inline km_vcpu_t* km_find_vcpu(int vcpu_id)
{
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      if (machine.vm_vcpus[i] != NULL && machine.vm_vcpus[i]->vcpu_id == vcpu_id) {
         return machine.vm_vcpus[i];
      }
   }
   return NULL;
}

static int km_vcpu_set_joining(km_vcpu_t* vcpu, void* val)
{
   vcpu->is_joining = (uint64_t)val;
   return 0;
}

int km_pthread_join(pthread_t pid, km_kma_t ret)
{
   km_vcpu_t* vcpu;
   int rc;

   if (pid >= KVM_MAX_VCPUS) {
      return -ESRCH;
   }
   /*
    * Mark current thread as "joining someone else". pthread_join is not interruptable, so self()
    * needs to be skipped when asking all vcpus to stop. Not that join will exit naturally with the
    * other thread exits  due to signal.
    */
   km_vcpu_apply_self(km_vcpu_set_joining, (void*)1);
   vcpu = machine.vm_vcpus[pid];
   /*
    * There are multiple condition such as deadlock or double join that are supposed to be detected,
    * we simply delegate that to system pthread_join().
    *
    * Note: there is no special theatment of main thread. 'man pthread_join' says:
    * "All of the threads in a process are peers: any thread can join with any other thread in the
    * process."
    */
   if ((rc = -pthread_join(vcpu->vcpu_thread, (void*)ret)) == 0) {
      km_vcpu_put(vcpu);
   } else {
      km_vcpu_apply_self(km_vcpu_set_joining, (void*)0);
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
   vcpu->is_paused = 1;
   if (pt_kma->detach_state == DT_DETACHED) {
      km_vcpu_put(vcpu);
   }
   // if (--machine.vm_vcpu_run_cnt == 0) {
   if (__atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST) == 0) {
      km_signal_machine_fini();
   }
}