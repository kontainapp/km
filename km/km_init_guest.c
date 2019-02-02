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

#include <stddef.h>
#include <string.h>

#include "km.h"

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
	void (*__f)(void *);
	void *__x;
	struct __ptcb *__next;
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
	struct km_pthread *self;
	uintptr_t *dtv;
	void *unused1, *unused2;
	uintptr_t sysinfo;
	uintptr_t canary, canary2;

	/* Part 2 -- implementation details, non-ABI. */
	int tid;
	int errno_val;
	volatile int detach_state;
	volatile int cancel;
	volatile unsigned char canceldisable, cancelasync;
	unsigned char tsd_used:1;
	unsigned char unblock_cancel:1;
	unsigned char dlerror_flag:1;
	unsigned char *map_base;
	size_t map_size;
	void *stack;
	size_t stack_size;
	size_t guard_size;
	void *start_arg;
	void *(*start)(void *);
	void *result;
	km__ptcb_t *cancelbuf;
	void **tsd;
	struct {
		volatile void *volatile head;
		long off;
		volatile void *volatile pending;
	} robust_list;
	volatile int timer_id;
	km__locale_t* locale;
	volatile int killlock[1];
	char *dlerror_buf;
	void *stdio_locks;

	/* Part 3 -- the positions of these fields relative to
	 * the end of the structure is external and internal ABI. */
	uintptr_t canary_at_end;
	uintptr_t *dtv_copy;
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
	void *space[16];
} km_builtin_tls_t;
#define MIN_TLS_ALIGN offsetof(km_builtin_tls_t, pt)

km_builtin_tls_t builtin_tls[1];

/*
 * load_elf() finds __libc by name in the ELF image of the guest. We follow __init_libc() logic to
 * initialize the content.
 *
 * Care needs to be taken to distinguish between addresses seen in the guest and in km. We have two
 * sets of variables for each structure we deal with, like libc and libc_kma, former and latter
 * correspondingly. The guest addresses are, as everywhere in km, of uint64_t. km addresses are of
 * the type there are in the guest. When we need to obtain addresses of subfields in the guests we
 * cast the uint64_t to the appropriate pointer, the use &(struct)->field.
 *
 * TODO: There are other guest structures, such as __environ, __hwcap, __sysinfo, __progname and so
 * on, we will need to process them as well most likely.
 */
uint64_t km_init_guest(void)
{
   uint64_t libc = km_guest.km_libc.st_value;
   km__libc_t* libc_kma;
   uint64_t mem;
   km_pthread_t* tcb_kma;
   uint64_t tcb;

   // If libc isn't part of loaded ELF there is nothing for us to do
   if (libc == 0) {
      return 0;
   }
   libc_kma = km_gva_to_kma(libc);
   libc_kma->auxv = NULL;   // for now
   libc_kma->page_size = PAGE_SIZE;
   libc_kma->secure = 1;
   libc_kma->can_do_threads = 0;   // TODO: for now
   /*
    * TODO: km_main_tls should be initialized from ELF headers of PT_TLS, PT_PHDR ... type to get
    * information about guest program specific TLS. For now we go with minimal TLS just to support
    * pthreads and internal data.
    */
   libc_kma->tls_align = MIN_TLS_ALIGN;
   libc_kma->tls_size = 2 * sizeof(void*) + sizeof(km_pthread_t) + MIN_TLS_ALIGN;
   mem = km_mem_brk(0);
   if (km_mem_brk(mem + libc_kma->tls_size) != mem + libc_kma->tls_size) {
      err(2, "No memory for TLS");
   }
   tcb = rounddown(mem + libc_kma->tls_size - sizeof(km_pthread_t), libc_kma->tls_align);
   tcb_kma = (km_pthread_t*)km_gva_to_kml(tcb);
   tcb_kma->dtv = tcb_kma->dtv_copy = (uintptr_t*)mem;
   tcb_kma->self = (km_pthread_t*)tcb;
   tcb_kma->detach_state = DT_JOINABLE;
   tcb_kma->locale = &((km__libc_t*)libc)->global_locale;
   tcb_kma->robust_list.head = &((km_pthread_t*)tcb)->robust_list.head;
   return tcb;
}