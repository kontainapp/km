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

#define _GNU_SOURCE   // Needed for clone(2) flag definitions.
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdlib.h>
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
 * is newer so keep both references).
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
   km_gva_t argv_km[argc + 1];   // argv to copy to guest stack_init
   argv_km[argc] = 0;
   for (int i = argc - 1; i >= 0; i--) {
      int len = strnlen(argv[i], PATH_MAX) + 1;

      stack_top -= len;
      if (map_base + GUEST_STACK_SIZE - stack_top > GUEST_ARG_MAX) {
         errx(2, "Argument list is too large");
      }
      argv_km[i] = stack_top;
      stack_top_kma -= len;
      strncpy(stack_top_kma, argv[i], len);
   }

   static const char* pstr = "X86_64";
   int pstr_len = strlen(pstr) + 1;
   stack_top -= pstr_len;
   stack_top_kma -= pstr_len;
   strcpy(stack_top_kma, pstr);
   km_gva_t platform_gva = stack_top;

   // TODO: AT_RANDOM random data for seeds (16 bytes)

   /*
    * ABI wants the stack 16 bytes aligned at the end of this, when we place
    * argc on the stack. From this point down we are going to place a null AUXV
    * entry (8 bytes), AUXV, env pointers and zero entry, argv pointers and zero
    * entry, and finally argc. AUXV entries are 16 bytes so they don't change the
    * alignment. env and argv pointers are 8 bytes though, so we adjust for
    * evenness of them together.
    *
    * Reference: https://www.uclibc.org/docs/psABI-x86_64.pdf
    */
   stack_top = rounddown(stack_top, sizeof(void*) * 2);
   if ((argc + envc) % 2 != 0) {
      stack_top -= sizeof(void*);
   }
   stack_top_kma = km_gva_to_kma_nocheck(stack_top);
   void* auxv_end = stack_top_kma;
   // AUXV
#define NEW_AUXV_ENT(type, val)                                                                    \
   {                                                                                               \
      stack_top -= 2 * sizeof(void*);                                                              \
      stack_top_kma -= 2 * sizeof(void*);                                                          \
      uint64_t* ptr = stack_top_kma;                                                               \
      ptr[0] = (type);                                                                             \
      ptr[1] = (uint64_t)(val);                                                                    \
   }

   NEW_AUXV_ENT(0, 0);
   NEW_AUXV_ENT(AT_PLATFORM, platform_gva);
   NEW_AUXV_ENT(AT_EXECFN, argv_km[0]);
   // TODO: AT_RANDOM
   NEW_AUXV_ENT(AT_SECURE, 0);
   NEW_AUXV_ENT(AT_EGID, 0);
   NEW_AUXV_ENT(AT_GID, 0);
   NEW_AUXV_ENT(AT_EUID, 0);
   NEW_AUXV_ENT(AT_UID, 0);
   NEW_AUXV_ENT(AT_ENTRY, km_guest.km_ehdr.e_entry + km_guest.km_load_adjust);
   NEW_AUXV_ENT(AT_FLAGS, 0);
   if (km_dynlinker.km_filename != 0) {
      NEW_AUXV_ENT(AT_BASE, km_dynlinker.km_load_adjust);
   }
   NEW_AUXV_ENT(AT_PHNUM, km_guest.km_ehdr.e_phnum);
   NEW_AUXV_ENT(AT_PHENT, km_guest.km_ehdr.e_phentsize);
   /*
    * Set AT_PHDR. Prefer PT_PHDR if it exists use it. If no PT_PHDR exists
    * set based on first PT_LOAD found.
    */
   int phdr_found = 0;
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type == PT_PHDR) {
         NEW_AUXV_ENT(AT_PHDR, km_guest.km_phdr[i].p_vaddr + km_guest.km_load_adjust);
         phdr_found = 1;
         break;
      }
   }
   if (phdr_found == 0) {
      for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
         if (km_guest.km_phdr[i].p_type == PT_LOAD) {
            NEW_AUXV_ENT(AT_PHDR,
                         km_guest.km_ehdr.e_phoff + km_guest.km_phdr[i].p_vaddr +
                             km_guest.km_load_adjust);
            phdr_found = 1;
            break;
         }
      }
   }
   assert(phdr_found != 0);
   NEW_AUXV_ENT(AT_CLKTCK, sysconf(_SC_CLK_TCK));
   NEW_AUXV_ENT(AT_PAGESZ, KM_PAGE_SIZE);
   // TODO: AT_HWCAP
   // TODO: AT_SYSINFO_EHDR
#undef NEW_AUXV_ENT

   // A safe copy of auxv for coredump (if needed)
   machine.auxv_size = auxv_end - stack_top_kma;
   machine.auxv = malloc(machine.auxv_size);
   assert(machine.auxv);
   memcpy(machine.auxv, stack_top_kma, machine.auxv_size);

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

/*
 * vcpu was obtained by km_vcpu_get() and has .is_used set. The registers and memory is fully
 * prepared to go. We need to create a thread if this is brand new vcpu, or signal the thread if it
 * is reused vcpu. We use .is_active to make sure the thread starts running when we really mean it.
 *
 * machine.vm_vcpu_run_cnt **is not**the same as number of used vcpu slots, it is a number of active
 * running vcpu threads. It is adjusted up right before vcpu thread starts, and down when an vcpu
 * exits, in km_vcpu_stopped(). If the vm_vcpu_run_cnt drops to 0 there are no running vcpus any
 * more, payload is done.
 *
 * Don't confuse .is_running with .is_active. The latter is set here and means there is active
 * thread running on that vcpu. .is_running is set/cleared when we enter/exit ioctl(KMV_RUN), so it
 * indicates that vcpu is executing payload instruction.
 */
int km_run_vcpu_thread(km_vcpu_t* vcpu, void* run(km_vcpu_t*))
{
   int rc = 0;

   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
   if (vcpu->vcpu_thread == 0) {
      pthread_attr_t vcpu_thr_att;

      km_pthread_attr_init(&vcpu_thr_att);
      km_pthread_attr_setstacksize(&vcpu_thr_att, 16 * KM_PAGE_SIZE);
      km_lock_vcpu_thr(vcpu);
      vcpu->is_active = 1;
      if ((rc = -pthread_create(&vcpu->vcpu_thread, &vcpu_thr_att, (void* (*)(void*))run, vcpu)) != 0) {
         vcpu->is_active = 0;
      }
      km_unlock_vcpu_thr(vcpu);
      km_pthread_attr_destroy(&vcpu_thr_att);
   } else {
      km_lock_vcpu_thr(vcpu);
      vcpu->is_active = 1;
      km_pthread_cond_signal(&vcpu->thr_cv);
      km_unlock_vcpu_thr(vcpu);
   }
   if (rc != 0) {
      __atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt--
      err(1, "run_vcpu_thread: failed activating vcpu thread");
   }
   return rc;
}

void km_vcpu_stopped(km_vcpu_t* vcpu)
{
   km_lock_vcpu_thr(vcpu);
   km_exit(vcpu);   // release user space thread list lock
   km_vcpu_put(vcpu);

   // if (--machine.vm_vcpu_run_cnt == 0) {
   if (__atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST) == 0) {
      km_signal_machine_fini();
   }
   if (machine.exit_group == 1) {
      km_unlock_vcpu_thr(vcpu);
      pthread_exit(NULL);
   }
   while (vcpu->is_active == 0) {
      km_pthread_cond_wait(&vcpu->thr_cv, &vcpu->thr_mtx);
   }
   km_unlock_vcpu_thr(vcpu);
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
   // want this on odd 8 byte boundary to account for clone trampoline.
   new_vcpu->stack_top -= (new_vcpu->stack_top + 8) % 16;
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
         *lptid = km_vcpu_get_tid(new_vcpu);
      }
   }

   // Obey set_child_tid protocol for pthreads. See 'clone(2)'
   int* gtid;
   if ((gtid = km_gva_to_kma(new_vcpu->set_child_tid)) != NULL) {
      *gtid = km_vcpu_get_tid(new_vcpu);
   }

   if (km_run_vcpu_thread(new_vcpu, km_vcpu_run) < 0) {
      km_vcpu_put(new_vcpu);
      return -EAGAIN;
   }

   return km_vcpu_get_tid(new_vcpu);
}

uint64_t km_set_tid_address(km_vcpu_t* vcpu, km_gva_t tidptr)
{
   vcpu->clear_child_tid = tidptr;
   return km_vcpu_get_tid(vcpu);
}

void km_exit(km_vcpu_t* vcpu)
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
