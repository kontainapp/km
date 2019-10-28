/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
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
 * This includes passing parameters to main.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_syscall.h"

/*
 * Allocate stack for main thread and initialize it according to ABI:
 * https://www.uclibc.org/docs/psABI-x86_64.pdf,
 * https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf (not sure which
 * is never so keep both references).
 *
 * We are not freeing the stack until we get to km_machine_fini(), similar to memory allocated by
 * km_load_elf() for executable and data part of the program.
 *
 * Care needs to be taken to distinguish between addresses seen in the guest and in km. We have two
 * sets of variables for each structure we deal with, like stack_top and stack_top_kma, former and
 * latter correspondingly. The guest addresses are, as everywhere in km, of km_gva_t (aka uint64_t).
 *
 * See TLS description as referenced in gcc manual https://gcc.gnu.org/onlinedocs/gcc-9.2.0/gcc.pdf
 * in section 6.64 Thread-Local Storage, at the time of this writing pointing at
 * https://www.akkadia.org/drepper/tls.pdf.
 *
 * Return the location of argv in guest
 */
km_gva_t km_init_main(km_vcpu_t* vcpu, int argc, char* const argv[], int envc, char* const envp[])
{
   km_gva_t map_base;

   assert(km_guest.km_handlers != 0);
   km_init_guest_idt(km_guest.km_handlers);
   if ((map_base = km_guest_mmap_simple(GUEST_STACK_SIZE)) < 0) {
      err(1, "Failed to allocate memory for main stack");
   }
   km_gva_t stack_top = map_base + GUEST_STACK_SIZE;
   km_kma_t stack_top_kma = km_gva_to_kma_nocheck(stack_top);

   // Set environ - copy strings and prep the envp array
   char* km_envp[envc + 1];
   memcpy(km_envp, envp, sizeof(km_envp));
   for (int i = 0; i < envc - 1; i++) {
      int len = strnlen(km_envp[i], PATH_MAX) + 1;

      stack_top -= len;
      stack_top_kma -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         errx(2, "Environment list is too large");
      }
      strncpy(stack_top_kma, km_envp[i], len);
      km_envp[i] = (char*)stack_top;
   }
   stack_top = rounddown(stack_top, sizeof(void*));
   stack_top_kma = km_gva_to_kma_nocheck(stack_top);

   // copy arguments and form argv array
   char* argv_km[argc + 1];   // argv to copy to guest stack_init
   argv_km[argc] = NULL;
   for (int i = argc - 1; i >= 0; i--) {
      int len = strnlen(argv[i], PATH_MAX) + 1;

      stack_top -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         errx(2, "Argument list is too large");
      }
      argv_km[i] = (char*)stack_top;
      stack_top_kma -= len;
      strncpy(stack_top_kma, argv[i], len);
   }
   stack_top = rounddown(stack_top, sizeof(void*));
   stack_top_kma = km_gva_to_kma_nocheck(stack_top);
   // AUXV
   static const int size_of_aux_slot = 2 * sizeof(void*);
   stack_top -= size_of_aux_slot;
   stack_top_kma -= size_of_aux_slot;
   memset(stack_top_kma, 0, size_of_aux_slot);
   stack_top -= size_of_aux_slot;
   stack_top_kma -= size_of_aux_slot;
   *(uint64_t*)stack_top_kma = AT_PHNUM;
   *(uint64_t*)(stack_top_kma + sizeof(void*)) = km_guest.km_ehdr.e_phnum;
   stack_top -= size_of_aux_slot;
   stack_top_kma -= size_of_aux_slot;
   *(uint64_t*)stack_top_kma = AT_PHENT;
   *(uint64_t*)(stack_top_kma + sizeof(void*)) = km_guest.km_ehdr.e_phentsize;
   stack_top -= size_of_aux_slot;
   stack_top_kma -= size_of_aux_slot;
   *(uint64_t*)stack_top_kma = AT_PHDR;
   *(uint64_t*)(stack_top_kma + sizeof(void*)) = km_guest.km_phdr[0].p_vaddr + km_guest.km_ehdr.e_phoff;

   // place envp array
   size_t envp_sz = sizeof(km_envp[0]) * envc;
   stack_top_kma -= envp_sz;
   stack_top -= envp_sz;
   memcpy(stack_top_kma, km_envp, envp_sz);

   // place argv array
   stack_top_kma -= sizeof(argv_km);
   stack_top -= sizeof(argv_km);
   memcpy(stack_top_kma, argv_km, sizeof(argv_km));
   // place argc
   stack_top_kma -= sizeof(uint64_t);
   stack_top -= sizeof(uint64_t);
   *(uint64_t*)stack_top_kma = argc;

   vcpu->stack_top = stack_top;
   return stack_top;   // argv in the guest
}

static inline int km_run_vcpu_thread(km_vcpu_t* vcpu, const km_kma_t restrict attr)
{
   int rc;

   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
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
      __atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt--
   }
   return rc;
}

void km_vcpu_stopped(km_vcpu_t* vcpu)
{
   km_vcpu_put(vcpu);

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
   while (vcpu->is_used == 0) {
      if (pthread_cond_wait(&vcpu->thr_cv, &vcpu->thr_mtx) != 0) {
         err(1, "wait on condition thr_cv");
      }
   }
   if (pthread_mutex_unlock(&vcpu->thr_mtx) != 0) {
      err(1, "unlock mutex thr_mtx");
   }
}

int km_clone(km_vcpu_t* vcpu,
             unsigned long flags,
             uint64_t child_stack,
             km_gva_t ptid,
             km_gva_t ctid,
             unsigned long newtls,
             void** cargs)
{
   // threads only
   if ((flags & CLONE_THREAD) == 0) {
      return -ENOTSUP;
   }

   km_vcpu_t* new_vcpu = km_vcpu_get();
   if (new_vcpu == NULL) {
      return -EAGAIN;
   }

   if ((flags & CLONE_CHILD_SETTID) != 0) {
      new_vcpu->set_child_tid = ctid;
   }
   if ((flags & CLONE_CHILD_CLEARTID) != 0) {
      new_vcpu->clear_child_tid = ctid;
   }

   new_vcpu->stack_top = (uintptr_t)child_stack;
   new_vcpu->guest_thr = newtls;
   int rc =
       km_vcpu_set_to_run(new_vcpu, km_guest.km_clone_child, (km_gva_t)cargs[0], (km_gva_t)cargs[1]);
   if (rc < 0) {
      km_vcpu_put(new_vcpu);
      return rc;
   }

   // Obey parent set tid protocol
   if ((flags & CLONE_PARENT_SETTID) != 0) {
      int* lptid = km_gva_to_kma(ptid);
      if (lptid != NULL) {
         *lptid = new_vcpu->vcpu_id + 1;
      }
   }

   // Obey set_child_tid protocol for pthreads. See 'clone(2)'
   int* gtid;
   if ((gtid = km_gva_to_kma(new_vcpu->set_child_tid)) != NULL) {
      *gtid = new_vcpu->vcpu_id + 1;
   }

   if (km_run_vcpu_thread(new_vcpu, NULL) < 0) {
      km_vcpu_put(new_vcpu);
      return -EAGAIN;
   }

   return new_vcpu->vcpu_id + 1;
}

uint64_t km_set_tid_address(km_vcpu_t* vcpu, km_gva_t tidptr)
{
   vcpu->clear_child_tid = tidptr;
   return vcpu->vcpu_id + 1;
}

void km_exit(km_vcpu_t* vcpu, int status)
{
   if (vcpu->clear_child_tid != 0) {
      // See 'man 2 set_tid_address'
      int* ctid = km_gva_to_kma(vcpu->clear_child_tid);
      if (ctid != NULL) {
         *ctid = 0;
         __syscall_6(SYS_futex, (uintptr_t)ctid, FUTEX_WAKE, 1, 0, 0, 0);
      }
   }
}