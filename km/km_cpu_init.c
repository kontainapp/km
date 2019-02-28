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

static void kvm_vcpu_init_sregs(int fd, uint64_t fs)
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
   if (ioctl(fd, KVM_SET_SREGS, &sregs) < 0) {
      err(1, "KVM: set sregs failed");
   }
}

static inline int km_put_new_vcpu(km_vcpu_t* new)
{
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      if (machine.vm_vcpus[i] == NULL) {
         machine.vm_vcpu_run_cnt++;
         machine.vm_vcpus[i] = new;
         return i;
      }
   }
   return -1;
}

/*
 * Create vcpu, map the control region, initialize sregs.
 * Set RIP, SP, RFLAGS, and RDI, clear the rest of the regs.
 * VCPU is ready to run starting with instruction @RIP
 * RDI aka the first function arg, tells it if we are in main (==0) or pthread(!=0)
 * RIP always points to __start_c__ which is always entry point in ELF.
 */
km_vcpu_t* km_vcpu_init(int is_pthread, km_gva_t sp, km_gva_t pt)
{
   km_vcpu_t* vcpu;

   /*
    * TODO: consider returning errors to be able to keep going instead of just
    * giving up
    */
   if ((vcpu = malloc(sizeof(km_vcpu_t))) == NULL) {
      err(1, "KVM: no memory for vcpu");
   }
   memset(vcpu, 0, sizeof(km_vcpu_t));
   if ((vcpu->vcpu_id = km_put_new_vcpu(vcpu)) < 0) {
      err(1, "KVM: too many vcpus");
   }
   if ((vcpu->kvm_vcpu_fd = ioctl(machine.mach_fd, KVM_CREATE_VCPU, vcpu->vcpu_id)) < 0) {
      err(1, "KVM: create vcpu %d failed", vcpu->vcpu_id);
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_CPUID2, machine.cpuid) < 0) {
      err(1, "KVM: set CPUID2 failed");
   }
   if ((vcpu->cpu_run =
            mmap(NULL, machine.vm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->kvm_vcpu_fd, 0)) ==
       MAP_FAILED) {
      err(1, "KVM: failed mmap VCPU %d control region", vcpu->vcpu_id);
   }
   kvm_vcpu_init_sregs(vcpu->kvm_vcpu_fd, pt);

   /* per ABI, make sure sp + 8 is 16 aligned */
   sp &= ~(7ul);
   sp -= (sp + 8) % 16;

   kvm_regs_t regs = {
       .rip = km_guest.km_ehdr.e_entry,
       .rdi = is_pthread,   // first function argument
       .rflags = X86_RFLAGS_FIXED,
       .rsp = sp,
   };
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_REGS, &regs) < 0) {
      err(1, "KVM: set regs failed");
   }
   vcpu->guest_thr = pt;
   vcpu->stack_top = sp;
   if (km_gdb_enabled()) {
      if ((vcpu->vcpu_chan = chan_init(0)) == NULL) {
         err(1, "Failed to create comm channel to vCPU thread");
      }
   }
   return vcpu;
}

/*
 * Release resources allocated in km_vcpu_init()
 */
void km_vcpu_fini(km_vcpu_t* vcpu)
{
   if (vcpu->vcpu_chan != NULL) {
      chan_dispose(vcpu->vcpu_chan);
   }
   if (vcpu->cpu_run != NULL) {
      (void)munmap(vcpu->cpu_run, machine.vm_run_size);
   }
   if (vcpu->kvm_vcpu_fd >= 0) {
      close(vcpu->kvm_vcpu_fd);
   }
   if (vcpu->map_base) {
      km_guest_munmap(vcpu->map_base, vcpu->map_size);
   }
   machine.vm_vcpus[vcpu->vcpu_id] = NULL;
   free(vcpu);
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
