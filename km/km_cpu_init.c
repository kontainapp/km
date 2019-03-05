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
};

static chan_t* machine_chan;

/*
 * km_machine_fini() will wait for signal on machine_chan to start tearing everything down. We
 * signal this channel when vpcu count drops to zero in km_vcpu_fini().
 */
void km_signal_machine_fini(void)
{
   chan_send(machine_chan, NULL);
}

/*
 * Release resources allocated in km_vcpu_init(). Used in km_machine_fini() for final cleanup.
 * For normal thread completion use km_vcpu_put()
 */
void km_vcpu_fini(km_vcpu_t* vcpu)
{
   km_pthread_fini(vcpu);
   if (vcpu->vcpu_chan != NULL) {
      chan_dispose(vcpu->vcpu_chan);
   }
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
   void* buf;

   chan_recv(machine_chan, &buf);
   chan_dispose(machine_chan);

   assert(machine.vm_vcpu_run_cnt == 0);

   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu = machine.vm_vcpus[i];

      if (vcpu != NULL) {
         km_vcpu_fini(vcpu);
      }
   }
   /* check if there are any memory regions */
   for (int i = KVM_USER_MEM_SLOTS - 1; i >= 0; i--) {
      kvm_mem_reg_t* mr = machine.vm_mem_regs[i];

      if (mr != NULL) {
         /* TODO: Do we need to "unplug" it from the VM? */
         if (mr->memory_size != 0) {
            km_page_free((void*)mr->userspace_addr, mr->memory_size);
         }
         free(mr);
         machine.vm_mem_regs[i] = NULL;
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
   if (km_gdb_enabled()) {
      if ((vcpu->vcpu_chan = chan_init(0)) == NULL) {
         warn("failed init gdb chan for vcpu %d", vcpu->vcpu_id);
         return -1;
      }
   }
   return 0;
}

/*
 * km_vcpu_get() atomically finds the first vcpu slot that can be used for a new vcpu and adjusts
 * vm_vcpu_run_cnt. It could be an empty slot, or previously used slot left by exited thread.
 *
 * Note that vm_vcpu_run_cnt **is not** the same as number of used vcpu slots. It is instead a
 * number of active running vcpu threads. It is adjusted down when an vcpu exits, in
 * km_vcpu_stopped(). If the vm_vcpu_run_cnt drops to 0 there are no running vcpus any more, payload
 * is done. km_vcpu_stopped() signals the main thread to tear down and exit the km.
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

   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
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
   __atomic_sub_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt--
   free(new);
   return NULL;
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
   km_gva_t sp;

   if ((rc = kvm_vcpu_init_sregs(vcpu->kvm_vcpu_fd, vcpu->guest_thr)) < 0) {
      return rc;
   }

   sp = vcpu->stack_top;
   assert((sp & 7) == 0);
   // TODO: per ABI, make sure sp + 8 is 16 aligned?
   // sp -= (sp + 8) % 16;

   kvm_regs_t regs = {
       .rip = km_guest.km_ehdr.e_entry,
       .rdi = is_main_argc,   // first function argument
       .rsi = sp,             // where we put argv
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

   machine_chan = chan_init(0);
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
            warnx("KVM: physical memory width %d", machine.cpuid->entries[i].eax & 0xff);
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
   }
   km_mem_init();
}
