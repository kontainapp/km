/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
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
#include <cpuid.h>
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
#include <sys/resource.h>

#include "km.h"
#include "km_filesys.h"
#include "km_gdb.h"
#include "km_guest.h"
#include "km_mem.h"
#include "x86_cpu.h"

// Set CPUID VendorId to this. 12 chars max (sans \0) to fit in 3 register: ebx,ecx,edx
static const char cpu_vendor_id[3 * sizeof(u_int32_t) + 1] = "Kontain";

km_machine_t machine = {
    .kvm_fd = -1,
    .vm_type = VM_TYPE_KVM,
    .mach_fd = -1,
    .brk_mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sigpending.head = TAILQ_HEAD_INITIALIZER(machine.sigpending.head),
    .sigfree.head = TAILQ_HEAD_INITIALIZER(machine.sigfree.head),
    .mmaps.free = TAILQ_HEAD_INITIALIZER(machine.mmaps.free),
    .mmaps.busy = TAILQ_HEAD_INITIALIZER(machine.mmaps.busy),
    .mmaps.mutex = PTHREAD_MUTEX_INITIALIZER,
    .pause_mtx = PTHREAD_MUTEX_INITIALIZER,
    .pause_cv = PTHREAD_COND_INITIALIZER,
};

/*
 * km_machine_fini() will wait for read from machine.shutdown_fd to start tearing
 * everything down. We write to shutdown_fd when vpcu count drops to zero in km_vcpu_fini().
 */
void km_signal_machine_fini(void)
{
   if (km_gdb_client_is_attached() != 0) {
      eventfd_write(machine.intr_fd, 1);   // unblock gdb main_loop poll()
   }
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
   km_wait_on_eventfd(machine.shutdown_fd);
   close(machine.shutdown_fd);
   assert(machine.vm_vcpu_run_cnt == 0);
   free(machine.auxv);
   if (km_guest.km_filename != NULL) {
      free(km_guest.km_filename);
   }
   if (km_guest.km_phdr != NULL) {
      free(km_guest.km_phdr);
   }
   if (km_dynlinker.km_phdr != NULL) {
      free(km_dynlinker.km_phdr);
   }
   km_mem_fini();
   km_signal_fini();
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
         km_guest_page_free(mr->guest_phys_addr, mem_siz);
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
   if (machine.intr_fd >= 0) {
      close(machine.intr_fd);
      machine.intr_fd = -1;
   }

   km_hcalls_fini();
   km_fs_fini();
}

static void km_init_syscall_handler(km_vcpu_t* vcpu)
{
   size_t allocsz = sizeof(struct kvm_msrs) + 3 * sizeof(struct kvm_msr_entry);
   struct kvm_msrs* msrs = calloc(allocsz, 1);

   msrs->nmsrs = 3;
   msrs->entries[0].index = MSR_IA32_FMASK;
   msrs->entries[1].index = MSR_IA32_LSTAR;
   msrs->entries[1].data = km_guest_kma_to_gva(&__km_syscall_handler);
   msrs->entries[2].index = MSR_IA32_STAR;
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_MSRS, msrs) < 0) {
      err(errno, "KVM_SET_MSRS");
   }
   free(msrs);
}

static void kvm_vcpu_init_sregs(km_vcpu_t* vcpu)
{
   assert(machine.idt != 0);
   vcpu->sregs = (kvm_sregs_t){
       .cr0 = X86_CR0_PE | X86_CR0_PG | X86_CR0_WP | X86_CR0_NE,
       .cr3 = RSV_MEM_START + RSV_PML4_OFFSET,
       .cr4 = X86_CR4_PSE | X86_CR4_PAE | X86_CR4_PGE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT,
       .efer = X86_EFER_LME | X86_EFER_LMA | X86_EFER_SCE,

       .cs = {.limit = 0xffffffff,
              .type = 9, /* Execute-only, accessed */
              .present = 1,
              .s = 1,
              .l = 1,
              .g = 1},
       .tr = {.type = 11, /* 64-bit TSS, busy */
              .present = 1},
       .fs.base = vcpu->guest_thr,
       .idt.base = machine.idt,
       .idt.limit = machine.idt_size,
       .gdt.base = machine.gdt,
       .gdt.limit = machine.gdt_size,
   };
   vcpu->sregs_valid = 1;
}

/*
 * Create vcpu with KVM, map the control region.
 */
static int km_vcpu_init(km_vcpu_t* vcpu)
{
   int rc;

   if ((vcpu->kvm_vcpu_fd = ioctl(machine.mach_fd, KVM_CREATE_VCPU, vcpu->vcpu_id)) < 0) {
      warn("KVM: create VCPU %d failed", vcpu->vcpu_id);
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
   km_init_syscall_handler(vcpu);
   vcpu->sigpending = (km_signal_list_t){.head = TAILQ_HEAD_INITIALIZER(vcpu->sigpending.head)};
   vcpu->thr_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   vcpu->thr_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
   return 0;
}

/*
 * km_vcpu_get() atomically finds the first vcpu slot that can be used for a new vcpu. It could be
 * previously used slot left by exited thread, or a new one. vcpu->is_used == 0 marks the unused slot.
 */
km_vcpu_t* km_vcpu_get(void)
{
   int unused;
   int i;

   // First look for previously used slots
   for (i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu = machine.vm_vcpus[i];

      if (__atomic_load_n(&vcpu, __ATOMIC_SEQ_CST) == NULL) {
         break;
      }
      // if (machine.vm_vcpus[i].is_used == 0) ...is_used = 1;
      unused = 0;
      if (__atomic_compare_exchange_n(&vcpu->is_used, &unused, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
         km_gdb_vcpu_state_init(vcpu);
         return vcpu;
      }
   }
   if (i >= KVM_MAX_VCPUS) {
      return NULL;
   }
   // no unused slots, need to alloc and init a new one
   km_vcpu_t* new;
   km_vcpu_t* old = NULL;

   if ((new = calloc(1, sizeof(km_vcpu_t))) == NULL) {
      return NULL;
   }

   new->is_used = 1;   // so it won't get snatched right after its inserted
   for (; i < KVM_MAX_VCPUS; i++) {
      // if (machine.vm_vcpus[i] == NULL) machine.vm_vcpus[i] = new;
      if (__atomic_compare_exchange_n(&machine.vm_vcpus[i], &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
         new->vcpu_id = i;
         if (km_vcpu_init(new) < 0) {
            __atomic_store_n(&machine.vm_vcpus[i], NULL, __ATOMIC_SEQ_CST);
            break;
         }
         km_gdb_vcpu_state_init(new);
         return new;
      }
   }
   km_err_msg(0, "VCPU allocation failed");
   km_vcpu_fini(new);
   // km_vcpu_fini(new) frees 'new'
   return NULL;
}

/*
 * Apply callback for each used VCPU.
 * Returns a sum of returned from func() - i.e. 0 if all return 0.
 *
 * Note: may skip VCPus if vcpu_get() was called on a different thread during the apply_all op
 */
int km_vcpu_apply_all(km_vcpu_apply_cb func, uint64_t data)
{
   int ret;
   int total = 0;

   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu;

      if ((vcpu = machine.vm_vcpus[i]) == NULL) {
         break;   // since we allocate vcpus sequentially, no reason to scan after NULL
      }
      if (vcpu->is_used == 1 && (ret = (*func)(vcpu, data)) != 0) {
         km_infox(KM_TRACE_VCPU, "func ret %d for VCPU %d", ret, vcpu->vcpu_id);
         total += ret;
      }
   }
   return total;
}

// fetch vcpu by thread tid, as returned by gettid(). This should match km_vcpu_get_tid()
km_vcpu_t* km_vcpu_fetch_by_tid(pid_t tid)
{
   km_vcpu_t* vcpu;

   if (tid > 0 && tid < KVM_MAX_VCPUS && (vcpu = machine.vm_vcpus[tid - 1]) != NULL) {
      return vcpu->is_used == 1 ? vcpu : NULL;
   }
   return NULL;
}

/*
 * Returns 1 if the vcpu is still running (i.e.not paused)
 */
static inline int km_vcpu_count_running(km_vcpu_t* vcpu, uint64_t unused)
{
   return vcpu->is_running;
}

/*
 * Force KVM to exit by sending a signal to vcpu thread. The signal handler can be a noop, just
 * need to exist.
 */
static int km_vcpu_pause(km_vcpu_t* vcpu, uint64_t unused)
{
   km_lock_vcpu_thr(vcpu);
   if (vcpu->is_active == 1) {
      if (pthread_kill(vcpu->vcpu_thread, KM_SIGVCPUSTOP) != 0) {
         warnx("vcpu %d, pthread_kill failed, errno %d", vcpu->vcpu_id, errno);
      }
      km_unlock_vcpu_thr(vcpu);
      km_infox(KM_TRACE_VCPU, "VCPU %d signalled to pause", vcpu->vcpu_id);
      return 0;
   }
   km_unlock_vcpu_thr(vcpu);
   return 0;
}

void km_vcpu_pause_all(void)
{
   int count;

   km_mutex_lock(&machine.pause_mtx);
   machine.pause_requested = 1;
   km_mutex_unlock(&machine.pause_mtx);
   km_vcpu_apply_all(km_vcpu_pause, 0);

   for (int i = 0; i < 100 && (count = km_vcpu_apply_all(km_vcpu_count_running, 0)) != 0; i++) {
      nanosleep(&_1ms, NULL);
      km_infox(KM_TRACE_VCPU, "waiting for KVM_RUN to exit - %d", i);
   }
   assert(count == 0);
}

/*
 * Thread completed. Release the resources that we can and mark the vcpu available for reuse by
 * setting vcpu->is_used to 0
 */
void km_vcpu_put(km_vcpu_t* vcpu)
{
   vcpu->guest_thr = 0;
   vcpu->stack_top = 0;
   vcpu->is_running = 0;
   vcpu->is_active = 0;
   __atomic_store_n(&machine.vm_vcpus[vcpu->vcpu_id]->is_used, 0, __ATOMIC_SEQ_CST);
}

/*
 * Set the vcpu to run - Initialize sregs and regs.
 * Set RIP, SP, RFLAGS, and function args - RDI, RSI, clear the rest of the regs.
 * VCPU is ready to run starting with instruction @RIP, RDI and RSI are the first two function arg.
 */
int km_vcpu_set_to_run(km_vcpu_t* vcpu, km_gva_t start, uint64_t arg)
{
   km_gva_t sp = vcpu->stack_top;   // where we put argv
   assert((sp & 0x7) == 0);

   kvm_vcpu_init_sregs(vcpu);
   vcpu->regs = (kvm_regs_t){
       .rip = start,
       .rdi = arg,   // first function argument
       .rflags = X86_RFLAGS_FIXED,
       .rsp = sp,
   };
   vcpu->regs_valid = 1;

   km_write_registers(vcpu);
   km_write_sregisters(vcpu);

   if (km_gdb_client_is_attached() != 0) {
      km_gdb_update_vcpu_debug(vcpu, 0);
   }
   return 0;
}

int km_vcpu_clone_to_run(km_vcpu_t* vcpu, km_vcpu_t* new_vcpu)
{
   kvm_vcpu_init_sregs(new_vcpu);

   /*
    * pretend we have hc_args on the stack so clone wrapper behaves the same wy for child and parent
    */
   km_gva_t sp = new_vcpu->stack_top - sizeof(km_hc_args_t);
   km_vcpu_sync_rip(vcpu);
   vcpu->regs_valid = 0;
   km_read_registers(vcpu);
   new_vcpu->regs = vcpu->regs;
   new_vcpu->regs.rsp = sp;
   *((uint64_t*)km_gva_to_kma_nocheck(sp)) = 0;   // hc return value
   new_vcpu->regs_valid = 1;

   km_write_registers(new_vcpu);
   km_write_sregisters(new_vcpu);

   if (km_gdb_client_is_attached() != 0) {
      km_gdb_update_vcpu_debug(new_vcpu, 0);
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
void km_machine_init(km_machine_init_params_t* params)
{
   int rc;
   struct stat sb;
   char linkbuf[16]; // no need for more than 16 bytes

   if (km_fs_init() < 0) {
      err(1, "KM: k_init_guest_files() failed");
   }
   if ((machine.intr_fd = eventfd(0, 0)) == -1) {
      err(1, "KM: Failed to create machine intr_fd");
   }
   if ((machine.shutdown_fd = eventfd(0, 0)) == -1) {
      err(1, "KM: Failed to create machine shutdown_fd");
   }
   if ((machine.kvm_fd = open("/dev/kvm", O_RDWR /* | O_CLOEXEC */)) < 0) {
      err(1, "KVM: Can't open /dev/kvm");
   }
   // identify type of virtual machine kvm or kkm
   if (lstat("/dev/kvm", &sb) < 0) {
      err(1, "KVM: stat on device failed");
   }
   if ((sb.st_mode & S_IFMT) == S_IFLNK) {
      memset(linkbuf, 0, sizeof(linkbuf));
      if (readlink("/dev/kvm", linkbuf, sizeof(linkbuf)) < 0) {
         err(1, "KVM: readlink failed of /dev/kvm");
      }
      if (strcmp(linkbuf, "/dev/kkm") == 0) {
         machine.vm_type = VM_TYPE_KKM;
      }
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
            calloc(1, sizeof(kvm_cpuid2_t) + CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2))) == NULL) {
      err(1, "KVM: no memory for CPUID");
   }
   machine.cpuid->nent = CPUID_ENTRIES;
   if (ioctl(machine.kvm_fd, KVM_GET_SUPPORTED_CPUID, machine.cpuid) < 0) {
      err(1, "KVM: get supported CPUID failed");
   }
   /*
    * Intel SDM, Vol3, Table 3-8. Information Returned by CPUID.
    * Get and save max CPU supported phys memory.
    * Check for 1GB pages support
    */
   for (int i = 0; i < machine.cpuid->nent; i++) {
      struct kvm_cpuid_entry2* entry = &machine.cpuid->entries[i];
      switch (entry->function) {
         case 0x80000008:
            km_infox(KM_TRACE_KVM, "KVM: physical memory width %d", entry->eax & 0xff);
            machine.guest_max_physmem = 1ul << (entry->eax & 0xff);
            break;
         case 0x80000001:
            machine.pdpe1g = ((entry->edx & 1ul << 26) != 0);
            if (machine.pdpe1g == 0 && params->force_pdpe1g != KM_FLAG_FORCE_DISABLE) {
               // The actual hardware should support it, otherwise expect payload to EXIT_SHUTDOWN
               km_infox(KM_TRACE_KVM, "PATCHING pdpe1g to 1");
               entry->edx |= (1ul << 26);
               machine.pdpe1g = 1;
            } else if (machine.pdpe1g == 1 && params->force_pdpe1g == KM_FLAG_FORCE_DISABLE) {
               km_infox(KM_TRACE_KVM, "PATCHING pdpe1g to 0");
               entry->edx &= ~(1ul << 26);
               machine.pdpe1g = 0;
            }
            break;
         case 0x80000002:
         case 0x80000003:
         case 0x80000004:
            __get_cpuid(entry->function, &entry->eax, &entry->ebx, &entry->ecx, &entry->edx);
            break;
         case 0x0:
            km_infox(KM_TRACE_KVM, "Setting VendorId to '%s'", cpu_vendor_id);
            memcpy(&entry->ebx, cpu_vendor_id, sizeof(u_int32_t));
            memcpy(&entry->edx, cpu_vendor_id + sizeof(u_int32_t), sizeof(u_int32_t));
            memcpy(&entry->ecx, cpu_vendor_id + 2 * sizeof(u_int32_t), sizeof(u_int32_t));
            break;
      }
   }
   if (machine.guest_max_physmem > GUEST_MAX_PHYSMEM_SUPPORTED) {
      km_infox(KM_TRACE_MEM,
               "Scaling down guest max phys mem to %#lx from %#lx",
               GUEST_MAX_PHYSMEM_SUPPORTED,
               machine.guest_max_physmem);
      machine.guest_max_physmem = GUEST_MAX_PHYSMEM_SUPPORTED;
   }
   if (machine.pdpe1g == 0) {
      /*
       * In the absence of 1gb pages we can only support 2GB, first for
       * text+data, and the second for the stack. See assert() in
       * set_pml4_hierarchy()
       */
      km_infox(KM_TRACE_MEM,
               "KVM: 1gb pages are not supported (pdpe1g=0), setting VM max mem to 2 GiB");
      machine.guest_max_physmem = MIN(2 * GIB, machine.guest_max_physmem);
   }
   if (params->guest_physmem != 0) {
      if ((machine.vm_type == VM_TYPE_KKM) && (params->guest_physmem != GUEST_MAX_PHYSMEM_SUPPORTED)) {
         errx(1, "Only 512GB physical memory supported with KKM driver");
      } else {
         if (params->guest_physmem > machine.guest_max_physmem) {
            errx(1,
                 "Cannot set guest memory size to '0x%lx'. Max supported=0x%lx",
                 params->guest_physmem,
                 machine.guest_max_physmem);
         }
         machine.guest_max_physmem = params->guest_physmem;
      }
   }
   km_mem_init(params);
   km_signal_init();
}
