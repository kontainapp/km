/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "x86_cpu.h"

km_machine_t machine = {
    .kvm_fd = -1,
    .mach_fd = -1,
    .brk_mutex = PTHREAD_MUTEX_INITIALIZER,
};

/*
 * km_machine_fini() will wait for read from machine.shutdown_fd to start tearing
 * everything down. We write to shutdown_fd when vpcu count drops to zero in km_vcpu_fini().
 */
void km_signal_machine_fini(void)
{
   if (eventfd_write(machine.shutdown_fd, 1) == -1) {
      errx(2, "Failed to send machine_fini signal");
   }
}

/*
 * Release resources allocated in km_vcpu_init(). Used in km_machine_fini() for final cleanup.
 * For normal thread completion use km_vcpu_put()
 */
void km_vcpu_fini(km_vcpu_t* vcpu)
{
   km_pthread_fini(vcpu);
   close(vcpu->eventfd);
   if (vcpu->cpu_run != NULL) {
      if (munmap(vcpu->cpu_run, machine.vm_run_size) != 0) {
         err(3, "munmap cpu_run for vcpu fd");
      }
   }
   if (vcpu->kvm_vcpu_fd >= 0) {
      if (close(vcpu->kvm_vcpu_fd) != 0) {
         err(3, "closing vcpu fd");
      }
   }
   machine.vm_vcpus[vcpu->vcpu_id] = NULL;
   free(vcpu);
}

/*
 * Join the vcpu threads, then dispose of all machine resources
 */
void km_machine_fini(void)
{
   eventfd_t buf;
   int ret;
   while ((ret = eventfd_read(machine.shutdown_fd, &buf)) == -1 && errno == EINTR)
      ;
   close(machine.shutdown_fd);
   assert(machine.vm_vcpu_run_cnt == 0);
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu;

      if ((vcpu = machine.vm_vcpus[i]) != NULL) {
         km_vcpu_fini(vcpu);
      }
   }
   /* check if there are any memory regions */
   for (int i = KM_MEM_SLOTS - 1; i >= 0; i--) {
      kvm_mem_reg_t* mr = &machine.vm_mem_regs[i];

      if (mr->memory_size != 0) {
         uint64_t mem_siz = mr->memory_size;
         mr->memory_size = 0;
         if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, mr) < 0) {
            warn("KVM: failed to unplug memory region %d", i);
         }
         km_page_free((void*)mr->userspace_addr, mem_siz);
         mr->userspace_addr = 0;
      }
   }
   /* Now undo things done in km_machine_init */
   if (machine.cpuid != NULL) {
      free(machine.cpuid);
      machine.cpuid = NULL;
   }
   if (machine.mach_fd >= 0) {
      close(machine.mach_fd);
      machine.mach_fd = -1;
   }
   if (machine.kvm_fd >= 0) {
      close(machine.kvm_fd);
      machine.kvm_fd = -1;
   }
   km_hcalls_fini();
}

static int kvm_vcpu_init_sregs(int fd, uint64_t fs)
{
   static const kvm_sregs_t sregs_template = {
       .cr0 = X86_CR0_PE | X86_CR0_PG | X86_CR0_WP | X86_CR0_NE,
       .cr3 = RSV_MEM_START + RSV_PML4_OFFSET,
       .cr4 = X86_CR4_PSE | X86_CR4_PAE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT,
       .efer = X86_EFER_LME | X86_EFER_LMA,

       .cs = {.limit = 0xffffffff,
              .type = 9, /* Execute-only, accessed */
              .present = 1,
              .s = 1,
              .l = 1,
              .g = 1},
       .tr = {.type = 11, /* 64-bit TSS, busy */
              .present = 1},
   };
   kvm_sregs_t sregs = sregs_template;

   sregs.fs.base = fs;
   return ioctl(fd, KVM_SET_SREGS, &sregs);
}

/*
 * Create vcpu with KVM, map the control region.
 */
static int km_vcpu_init(km_vcpu_t* vcpu)
{
   int rc;

   if ((vcpu->kvm_vcpu_fd = ioctl(machine.mach_fd, KVM_CREATE_VCPU, vcpu->vcpu_id)) < 0) {
      warn("KVM: create vcpu %d failed", vcpu->vcpu_id);
      return vcpu->kvm_vcpu_fd;
   }
   if ((rc = ioctl(vcpu->kvm_vcpu_fd, KVM_SET_CPUID2, machine.cpuid)) < 0) {
      warn("KVM: set CPUID2 failed");
      return rc;
   }
   if ((vcpu->cpu_run =
            mmap(NULL, machine.vm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->kvm_vcpu_fd, 0)) ==
       MAP_FAILED) {
      warn("KVM: failed mmap VCPU %d control region", vcpu->vcpu_id);
      return -1;
   }
   if (km_gdb_is_enabled()) {
      if ((vcpu->eventfd = eventfd(0, 0)) == -1) {
         warn("failed init gdb eventfd for vcpu %d", vcpu->vcpu_id);
         return -1;
      }
   }
   return 0;
}

/*
 * km_vcpu_get() atomically finds the first vcpu slot that can be used for a new vcpu. It could be
 * an empty slot, or previously used slot left by exited thread.
 *
 * Note that vm_vcpu_run_cnt **is not** the same as number of used vcpu slots, it is a number of
 * active running vcpu threads. It is adjusted up right before vcpu thread starts, and down when an
 * vcpu exits, in km_vcpu_stopped(). If the vm_vcpu_run_cnt drops to 0 there are no running vcpus any
 * more, payload is done. km_vcpu_stopped() signals the main thread to tear down and exit the km.
 *
 * vcpu slot is still in use after a joinable thread exited but pthread_join() isn't executed yet.
 * Once the thread is joined in km_pthread_join(), the slot is reusable, i.e is_used is set to 0.
 *
 * We allocate new vcpu structure to use in an empty slot, and free it if a slot for reuse is found.
 */
km_vcpu_t* km_vcpu_get(void)
{
   km_vcpu_t* new;
   km_vcpu_t* old;
   int unused;

   if ((new = malloc(sizeof(km_vcpu_t))) == NULL) {
      err(1, "KVM: no memory for vcpu");
   }
   memset(new, 0, sizeof(km_vcpu_t));

   new->is_used = 1;   // so it won't get snatched right after its inserted
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      // if (machine.vm_vcpus[i] == NULL) machine.vm_vcpus[i] = new;
      old = NULL;
      if (__atomic_compare_exchange_n(&machine.vm_vcpus[i], &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
         new->vcpu_id = i;
         if (km_vcpu_init(new) < 0) {
            __atomic_store_n(&machine.vm_vcpus[i], NULL, __ATOMIC_SEQ_CST);
            break;
         }
         return new;
      }
      // if (machine.vm_vcpus[i].is_used == 0) ...is_used = 1;
      unused = 0;
      if (__atomic_compare_exchange_n(&old->is_used, &unused, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
         free(new);   // no need, reusing an existing one
         return old;
      }
   }
   free(new);
   return NULL;
}

/*
 * Apply callback for each used VCPU.
 * Returns a sum of returned from func() - i.e. 0 if all return 0.
 *
 * Note: may skip VCPus if vcpu_get() was called on a different thread during the apply_all op
 */
int km_vcpu_apply_all(km_vcpu_apply_cb func, void* data)
{
   int ret, i, total;

   for (i = 0, total = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu;

      if ((vcpu = machine.vm_vcpus[i]) == NULL) {
         break;   // since we allocate vcpus sequentially, no reason to scan after NULL
      }
      if (vcpu->is_used == 1 && (ret = (*func)(vcpu, data)) != 0) {
         km_infox("km_vcpu_apply_all: func %p returned %d for vcpu %d", func, ret, vcpu->vcpu_id);
         total += ret;
      }
   }
   return total;
}

/*
 * Returns 1 if the vcpu is still running (i.e.not paused). Skip the ones waiting on join as join is
 * not interruptable anyways and may generate a deadlock.
 */
static int km_vcpu_count_running(km_vcpu_t* vcpu, void* unused)
{
   if (vcpu->is_paused == 1 || vcpu->is_joining == 1) {
      return 0;
   }
   return 1;
}

void km_vcpu_wait_for_all_to_pause(void)
{
   int count;
   while ((count = km_vcpu_apply_all(km_vcpu_count_running, 0)) != 0) {
      // Wait for vcpus to exit from KVM. No need for speed here so can do busy wait.
      static const struct timespec req = {
          .tv_sec = 0, .tv_nsec = 10000000, /* 10 millisec */
      };
      km_infox("Still %d vcpus running", count);
      nanosleep(&req, NULL);
   }
   machine.pause_requested = 0;
}

/*
 * Convenience wrapper to force KVM exit by setting immediate exit flag and then sending a signal to
 * vcpu thread. The thread signal handler can be a noop, just need to exist.
 * Note:  Relies on the upstairs to only be called on a used VCPU.
 */
int km_vcpu_pause(km_vcpu_t* vcpu, void* unused)
{
   int ret;
   if (vcpu->is_paused == 1 || vcpu->vcpu_thread == 0) {   // already paused or not started yet
      km_infox("VCPU %d skipped (p=%d thr=0x%lx)", vcpu->vcpu_id, vcpu->is_paused, vcpu->vcpu_thread);
      return 0;
   }
   vcpu->cpu_run->immediate_exit = 1;
   if ((ret = pthread_kill(vcpu->vcpu_thread, KM_SIGVCPUSTOP)) != 0) {
      warn("%s: Failed to send stop to vCPU %d (errno %d)", __FUNCTION__, vcpu->vcpu_id, ret);
      return 1;
   }
   km_infox("VCPU %d signalled to pause", vcpu->vcpu_id);
   return 0;
}

/*
 * Thread completed. Release the resources that we can and mark the vcpu available for reuse by
 * setting vcpu->is_used to 0
 */
void km_vcpu_put(km_vcpu_t* vcpu)
{
   km_pthread_fini(vcpu);
   // vcpu->is_used = 0;
   __atomic_store_n(&machine.vm_vcpus[vcpu->vcpu_id]->is_used, 0, __ATOMIC_SEQ_CST);
}

/*
 * Set the vcpu to run - Initialize sregs and regs.
 * Set RIP, SP, RFLAGS, and RDI, clear the rest of the regs.
 * VCPU is ready to run starting with instruction @RIP RDI aka the first function arg,
 * tells it if we are in main with argc (>0) or pthread(==0) RIP always points to __start_c__
 * which is always entry point in ELF.
 */
int km_vcpu_set_to_run(km_vcpu_t* vcpu, int is_main_argc)
{
   int rc;
   km_gva_t sp, argv;

   if ((rc = kvm_vcpu_init_sregs(vcpu->kvm_vcpu_fd, vcpu->guest_thr)) < 0) {
      return rc;
   }

   argv = sp = vcpu->stack_top;
   assert((sp & 7) == 0);
   sp -= (sp + 8) % 16;   // per ABI, make sure sp + 8 is 16 aligned

   kvm_regs_t regs = {
       .rip = km_guest.km_ehdr.e_entry,
       .rdi = is_main_argc,   // first function argument
       .rsi = argv,           // where we put argv
       .rflags = X86_RFLAGS_FIXED,
       .rsp = sp,
   };
   if ((rc = ioctl(vcpu->kvm_vcpu_fd, KVM_SET_REGS, &regs)) < 0) {
      return rc;
   }
   return 0;
}

/*
 * initial steps setting our VM
 *
 * talk to KVM
 * create VM
 * set VM run memory region
 * prepare cpuid
 *
 * Any failure is fatal, hence void
 */
void km_machine_init(void)
{
   int rc;

   if ((machine.shutdown_fd = eventfd(0, 0)) == -1) {
      err(1, "KM: Failed to create machine shutdown_fd");
   }
   if ((machine.kvm_fd = open("/dev/kvm", O_RDWR /* | O_CLOEXEC */)) < 0) {
      err(1, "KVM: Can't open /dev/kvm");
   }
   if ((rc = ioctl(machine.kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
      err(1, "KVM: get API version failed");
   }
   if (rc != KVM_API_VERSION) {
      errx(1, "KVM: API version mismatch");
   }
   if ((machine.mach_fd = ioctl(machine.kvm_fd, KVM_CREATE_VM, NULL)) < 0) {
      err(1, "KVM: create VM failed");
   }
   if ((machine.vm_run_size = ioctl(machine.kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0)) < 0) {
      err(1, "KVM: get VM memory region size failed");
   }
   if (machine.vm_run_size < sizeof(kvm_run_t)) {
      errx(1,
           "KVM: suspicious VM memory region size %zu, expecting at least %zu",
           machine.vm_run_size,
           sizeof(kvm_run_t));
   }
   if ((machine.cpuid =
            malloc(sizeof(kvm_cpuid2_t) + CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2))) == NULL) {
      err(1, "KVM: no memory for CPUID");
   }
   machine.cpuid->nent = CPUID_ENTRIES;
   if (ioctl(machine.kvm_fd, KVM_GET_SUPPORTED_CPUID, machine.cpuid) < 0) {
      err(1, "KVM: get supported CPUID failed");
   }
   /*
    * SDM, Table 3-8. Information Returned by CPUID.
    * Get and save max CPU supported phys memory.
    * Check for 1GB pages support
    */
   for (int i = 0; i < machine.cpuid->nent; i++) {
      switch (machine.cpuid->entries[i].function) {
         case 0x80000008:
            km_infox("KVM: physical memory width %d", machine.cpuid->entries[i].eax & 0xff);
            machine.guest_max_physmem = 1ul << (machine.cpuid->entries[i].eax & 0xff);
            break;
         case 0x80000001:
            machine.pdpe1g = ((machine.cpuid->entries[i].edx & 1ul << 26) != 0);
            break;
      }
   }
   if (machine.pdpe1g == 0) {
      /*
       * In the absence of 1gb pages we can only support 2GB, first for
       * text+data, and the second for the stack. See assert() in
       * set_pml4_hierarchy()
       */
      machine.guest_max_physmem = MIN(2 * GIB, machine.guest_max_physmem);
      km_infox("KVM: 1gb pages are not supported");
   }
   km_mem_init();
}
