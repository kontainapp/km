/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include "km_mem.h"
#include "km_exec.h"
#include "km_hcalls.h"
#include "km_filesys.h"
#include "km_elf.h"

// Remove when no longer needed.
void km_memory_dump(const char* tag)
{

   extern void km_mmap_dump_lists(const char* tag);

   km_mmap_dump_lists(tag);
   km_infox(KM_TRACE_HC, "brk 0x%lx, tbrk 0x%lx", machine.brk, machine.tbrk);
   for (int i = 0; i < KM_MEM_SLOTS; i++) {
      if (machine.vm_mem_regs[i].memory_size != 0) {
         km_infox(KM_TRACE_HC, "vm_mem_regs[%d]: slot %u, flags 0x%x, guest_phys_addr 0x%llx, memory_size %llu, userspace_addr 0x%llx",
               i, machine.vm_mem_regs[i].slot, machine.vm_mem_regs[i].flags, machine.vm_mem_regs[i].guest_phys_addr,
               machine.vm_mem_regs[i].memory_size, machine.vm_mem_regs[i].userspace_addr);
      }
   }
}

/*
 * This global is setup by execve_hcall() or execveat_hcall(), then all vcpu's are parked,
 * then control is given to the km main thread which looks at this
 * data and makes the rest of exec() happen.
 * TODO: there is no mutex serializing access to this data.  If we
 * ever discover that multiple threads are running execve(), we
 * will need to serialize here.
 */
km_exec_state_t km_exec_state;

/*
 * Copy argv[] and envp[] into allocated km memory.  Pointers to the
 * allocated memory are returned in km_exec_state.xxx
 * argv[] and envp are gva's
 * The caller is responsible for freeing the allocated memory.
 * Returns:
 *   0 - success
 *   errno - failure
 */
int km_copy_argvenv_to_km(char* argv[], char* envp[])
{
   char** argpin = (char**)km_gva_to_kma((km_gva_t)argv);
   char** envpin = (char**)km_gva_to_kma((km_gva_t)envp);
   int argc;
   int envc;
   char** argpout;
   char** envpout;
   int i;
   char* wp;

   assert(km_exec_state.argp_envp == NULL);

   // How many bytes do we need to hold argv and envp
   size_t bytes_needed = 0;
   for (argc = 0; argpin[argc] != NULL; argc++) {
      // km_infox(KM_TRACE_HC, "argpin[%d] = %p, kma %p", argc, argpin[argc], km_gva_to_kma((km_gva_t)argpin[argc]));
      bytes_needed += strlen(km_gva_to_kma((km_gva_t)argpin[argc])) + 1;
   }
   bytes_needed += (argc + 1) * sizeof(uint8_t*);
   for (envc = 0; envpin[envc] != NULL; envc++) {
      // km_infox(KM_TRACE_HC, "envpin[%d] = %p, kma %p", envc, envpin[envc], km_gva_to_kma((km_gva_t)envpin[envc]));
      bytes_needed += strlen(km_gva_to_kma((km_gva_t)envpin[envc])) + 1;
   }
   bytes_needed += (envc + 1) * sizeof(uint8_t*);
   // km_infox(KM_TRACE_HC, "bytes_needed %ld", bytes_needed);

   // Memory to hold argv and env
   km_exec_state.argp_envp = malloc(bytes_needed);
   if (km_exec_state.argp_envp == NULL) {
      return ENOMEM;
   }

   // Copy argv and envp out of the payload into km's memory
   argpout = (char**)km_exec_state.argp_envp;
   wp = (char*)&argpout[argc + 1];
   for (i = 0; argpin[i] != NULL; i++) {
       argpout[i] = wp;
       strcpy(wp, km_gva_to_kma((km_gva_t)argpin[i]));
       wp += strlen(wp) + 1;
   }
   argpout[i] = NULL;
   km_exec_state.argc = i;
   km_exec_state.argp = argpout;

   envpout = (char**)wp;
   wp = (char*)&envpout[envc + 1];
   for (i = 0; envpin[i] != NULL; i++) {
      envpout[i] = wp;
      strcpy(wp, km_gva_to_kma((km_gva_t)envpin[i]));
      wp += strlen(wp) + 1;
   }
   envpout[i] = NULL;
   km_exec_state.envc = i;
   km_exec_state.envp = envpout;

   return 0;
}

/*
 * Called from the km main thread once it gains control from
 * payload termination caused by a call to execve().
 */
void km_reinit_for_exec(void)
{

   // Close guest fd's that are marked close on exec.
   for (int guestfd = 0; guestfd < machine.filesys.nfdmap; guestfd++) {
      if (machine.filesys.guestfd_to_hostfd_map[guestfd] >= 0) {
         int flags = fcntl(machine.filesys.guestfd_to_hostfd_map[guestfd], F_GETFD);
         if (flags >= 0) {
            if ((flags & FD_CLOEXEC) != 0) {
               km_infox(KM_TRACE_HC, "Close guest fd %d, hostfd %d", guestfd, machine.filesys.guestfd_to_hostfd_map[guestfd]);
               int rc = km_fs_close(NULL, guestfd);
               if (rc != 0) {
                  km_info(KM_TRACE_HC, "close guest fd %d failed", guestfd);
               }
            }
         } else {
            km_info(KM_TRACE_HC, "fcntl on guestfd %d failed", guestfd);
         }
      }
   }

   // Set guest signal handlers back to default
   for (int i = 0; i < _NSIG; i++) {
      machine.sigactions[i].handler = (km_gva_t)SIG_DFL;
      machine.sigactions[i].sa_flags = 0;
      machine.sigactions[i].restorer = (km_gva_t)NULL;
      machine.sigactions[i].sa_mask = 0;
   }

   // Reset payload and dynlinker info
   km_payload_deinit(&km_dynlinker);
   km_payload_deinit(&km_guest);

   // Move the lower memory break address back to initial location.
   // This should free code and heap.  We keep the stack area since
   // we are keeping the vcpu's and they have stacks assigned to them.
   // Note that vdso and km_guest pages still exist.

km_memory_dump("Before code and heap free");

   km_mem_brk(GUEST_MEM_START_VA);

km_memory_dump("After code and heap free");
#if 0
   // This doesn't seem to free the stack area.  Using km_guest_munmap() for now.
   km_mem_tbrk(GUEST_MEM_TOP_VA - (2 * MIB));
#else
   int rc = km_guest_munmap(NULL, machine.vm_mem_regs[33].userspace_addr, machine.vm_mem_regs[33].memory_size);
   assert(rc == 0);
#endif
km_memory_dump("After stack free");

   // We intentionally leave the vm and vcpu definitions as they are.
   // Deleting vcpu's is a high overhead operation and the exec'ed
   // program will need the vm and at least one vcpu.

   // Loading the payload and setting up the stack is handled in km's main()
}
