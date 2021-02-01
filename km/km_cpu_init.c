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

#include <assert.h>
#include <cpuid.h>
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
#include <sys/utsname.h>

#include "km.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_gdb.h"
#include "km_guest.h"
#include "km_kkm.h"
#include "km_mem.h"
#include "x86_cpu.h"

// Set CPUID VendorId to this. 12 chars max (sans \0) to fit in 3 register: ebx,ecx,edx
static const char cpu_vendor_id[3 * sizeof(u_int32_t) + 1] = "Kontain";

km_machine_t machine = {
    .kvm_fd = -1,
    .vm_type = VM_TYPE_KVM,
    .mach_fd = -1,
    .vm_idle_vcpus.head = SLIST_HEAD_INITIALIZER(machine.vm_idle_vcpus.head),
    .vm_vcpu_mtx = PTHREAD_MUTEX_INITIALIZER,
    .brk_mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sigpending.head = TAILQ_HEAD_INITIALIZER(machine.sigpending.head),
    .sigfree.head = TAILQ_HEAD_INITIALIZER(machine.sigfree.head),
    .mmaps.free = TAILQ_HEAD_INITIALIZER(machine.mmaps.free),
    .mmaps.busy = TAILQ_HEAD_INITIALIZER(machine.mmaps.busy),
    .mmaps.mutex = PTHREAD_MUTEX_INITIALIZER,
    .pause_mtx = PTHREAD_MUTEX_INITIALIZER,
    .pause_cv = PTHREAD_COND_INITIALIZER,
    .ppid = 1,
    .pid = 1,
    .next_pid = 1000,
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
      km_errx(2, "Failed to send machine_fini signal");
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
         km_err(3, "munmap cpu_run for vcpu fd");
      }
   }
   if (vcpu->kvm_vcpu_fd >= 0) {
      if (close(vcpu->kvm_vcpu_fd) != 0) {
         km_err(3, "closing vcpu fd");
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
   close(machine.shutdown_fd);
   assert(km_vcpu_run_cnt() == 0);
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
            km_warn("KVM: failed to unplug memory region %d", i);
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
   char tmp[sizeof(struct kvm_msrs) + 3 * sizeof(struct kvm_msr_entry)] = {};
   struct kvm_msrs* msrs = (struct kvm_msrs*)tmp;

   msrs->nmsrs = 3;
   msrs->entries[0].index = MSR_IA32_FMASK;
   msrs->entries[1].index = MSR_IA32_LSTAR;
   msrs->entries[1].data = km_guest_kma_to_gva(&__km_syscall_handler);
   msrs->entries[2].index = MSR_IA32_STAR;
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_MSRS, msrs) < 0) {
      km_err(1, "KVM_SET_MSRS");
   }
   km_infox(KM_TRACE_VCPU, "__km_syscall_handler gva 0x%llx", msrs->entries[1].data);
}

static inline uint64_t rdtsc(void)
{
   uint64_t edx, eax;
   __asm__ __volatile__("rdtsc" : "=a"(eax), "=d"(edx));
   return edx << 32 | eax;
}

// Set the TSC value to that of the physical machine to make clock_gettime VDSO logic happy
static void km_init_tsc(km_vcpu_t* vcpu)
{
   char tmp[sizeof(struct kvm_msrs) + sizeof(struct kvm_msr_entry)] = {};
   struct kvm_msrs* msrs = (struct kvm_msrs*)tmp;

   msrs->nmsrs = 1;
   msrs->entries[0].index = MSR_IA32_TSC;
   msrs->entries[0].data = rdtsc();
   // TODO: Consider adding some value here to compensate for the time it takes to set the value
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_MSRS, msrs) < 0) {
      km_err(1, "KVM_SET_MSRS for TSC");
   }
}

void kvm_vcpu_init_sregs(km_vcpu_t* vcpu)
{
   assert(machine.idt != 0);
   vcpu->sregs = (kvm_sregs_t){
       .cr0 = X86_CR0_PE | X86_CR0_PG | X86_CR0_WP | X86_CR0_NE,
       .cr3 = RSV_MEM_START,
       .cr4 = X86_CR4_PSE | X86_CR4_PAE | X86_CR4_PGE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT |
              X86_CR4_OSXSAVE,
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
       .gs.base = km_guest_kma_to_gva(&km_hcargs[HC_ARGS_INDEX(vcpu->vcpu_id)]),
       .idt.base = machine.idt,
       .idt.limit = machine.idt_size,
       .gdt.base = machine.gdt,
       .gdt.limit = machine.gdt_size,
   };
   vcpu->sregs_valid = 1;
   km_infox(KM_TRACE_VCPU,
            "vcpu_id %d, sregs.gs.base: gva 0x%llx, kma %p",
            vcpu->vcpu_id,
            vcpu->sregs.gs.base,
            &km_hcargs[HC_ARGS_INDEX(vcpu->vcpu_id)]);
}

/*
 * Create vcpu with KVM, map the control region.
 */
static int km_vcpu_init(km_vcpu_t* vcpu)
{
   int rc;

   if ((vcpu->kvm_vcpu_fd = km_internal_fd_ioctl(machine.mach_fd, KVM_CREATE_VCPU, vcpu->vcpu_id)) < 0) {
      km_warn("KVM: create VCPU %d failed", vcpu->vcpu_id);
      return vcpu->kvm_vcpu_fd;
   }
   if ((rc = ioctl(vcpu->kvm_vcpu_fd, KVM_SET_CPUID2, machine.cpuid)) < 0) {
      km_warn("KVM: set CPUID2 failed");
      return rc;
   }
   if ((vcpu->cpu_run =
            mmap(NULL, machine.vm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->kvm_vcpu_fd, 0)) ==
       MAP_FAILED) {
      km_warn("KVM: failed mmap VCPU %d control region", vcpu->vcpu_id);
      return -1;
   }
   km_init_syscall_handler(vcpu);
   if (vcpu->vcpu_id == 0) {
      km_init_tsc(vcpu);
   }
   vcpu->sigpending = (km_signal_list_t){.head = TAILQ_HEAD_INITIALIZER(vcpu->sigpending.head)};
   vcpu->thr_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   vcpu->thr_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
   vcpu->signal_wait_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
   return 0;
}

/*
 * KKM driver keeps debug and syscall state.
 * When KM reuses VCPU this state becomes stale.
 * This ioctl bring KKM and KM state to sync.
 */
static inline int km_vmmonitor_vcpu_init(km_vcpu_t* vcpu)
{
   int retval = 0;

   if (machine.vm_type == VM_TYPE_KKM) {
      km_kkm_vcpu_init(vcpu);
   }
   return retval;
}

/*
 * km_vcpu_get() finds vcpu slot that can be used for a new vcpu.
 * It could be previously used slot left by exited thread, or a new one. Previously used vcpus are
 * maintained in an SLIST. If that SLIST is empty we allocate and initialize a new one. When a
 * thread exits km_vcpu_put() will place the vcpu on the SLIST for reuse.
 */
km_vcpu_t* km_vcpu_get(void)
{
   km_vcpu_t* vcpu;

   km_mutex_lock(&machine.vm_vcpu_mtx);
   if ((vcpu = SLIST_FIRST(&machine.vm_idle_vcpus.head)) != 0) {
      assert(vcpu->state == PARKED_IDLE);
      SLIST_REMOVE_HEAD(&machine.vm_idle_vcpus.head, next_idle);
      machine.vm_vcpu_run_cnt++;
      vcpu->state = STARTING;
      km_mutex_unlock(&machine.vm_vcpu_mtx);
      km_gdb_vcpu_state_init(vcpu);
      km_vmmonitor_vcpu_init(vcpu);
      return vcpu;
   }
   // no idle VCPUs, try to allocate a new one
   if (machine.vm_vcpu_cnt == KVM_MAX_VCPUS || (vcpu = calloc(1, sizeof(km_vcpu_t))) == NULL) {
      km_mutex_unlock(&machine.vm_vcpu_mtx);
      return NULL;
   }
   km_infox(KM_TRACE_VCPU, "Allocating new vcpu-%d", machine.vm_vcpu_cnt);
   vcpu->vcpu_id = machine.vm_vcpu_cnt;

   if (km_vcpu_init(vcpu) != 0) {
      km_mutex_unlock(&machine.vm_vcpu_mtx);
      km_warnx("VCPU init failed");
      km_vcpu_fini(vcpu);
      return NULL;
   }
   machine.vm_vcpu_cnt++;
   machine.vm_vcpu_run_cnt++;
   vcpu->state = STARTING;
   km_gdb_vcpu_state_init(vcpu);
   machine.vm_vcpus[vcpu->vcpu_id] = vcpu;
   km_mutex_unlock(&machine.vm_vcpu_mtx);
   return vcpu;
}

/*
 * Gets a VCPU in a specific slot. Used by snapshot resume.
 * This is called at initialization time (single threaded)
 * so there is no need to worry about concurency;
 */
km_vcpu_t* km_vcpu_restore(int tid)
{
   int slot = tid - 1;

   if (slot < 0 || slot >= KVM_MAX_VCPUS) {
      return NULL;
   }

   if (machine.vm_vcpus[slot] != 0) {
      km_warnx("tid % d already allocated", tid);
      return NULL;
   }
   km_vcpu_t* vcpu = calloc(1, sizeof(km_vcpu_t));
   vcpu->state = STARTING;
   vcpu->vcpu_id = slot;
   if (km_vcpu_init(vcpu) < 0) {
      km_warnx("km_vcpu_init failed - cannot restore snapshot");
      km_vcpu_fini(vcpu);
      return NULL;
   }
   km_gdb_vcpu_state_init(vcpu);
   kvm_vcpu_init_sregs(vcpu);
   machine.vm_vcpus[slot] = vcpu;
   machine.vm_vcpu_run_cnt++;
   machine.vm_vcpu_cnt++;
   return vcpu;
}

/*
 * Apply callback for each used VCPU.
 * Returns a sum of returned from func() - i.e. 0 if all return 0.
 *
 * Note: may skip VCPus if vcpu_get() was called on a different thread during the apply_all op
 */
int km_vcpu_apply_all(km_vcpu_apply_cb func, void* data)
{
   int ret;
   int total = 0;

   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu;

      if ((vcpu = machine.vm_vcpus[i]) == NULL) {
         break;   // since we allocate vcpus sequentially, no reason to scan after NULL
      }
      if (vcpu->state != PARKED_IDLE && (ret = (*func)(vcpu, data)) != 0) {
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
      return vcpu->state != PARKED_IDLE ? vcpu : NULL;
   }
   return NULL;
}

/*
 * Force KVM to exit by sending a signal to vcpu thread that is IN_GUEST. The signal handler can be
 * a noop, just need to exist.
 * Returns 1 if the vcpu is IN_GUEST and 0 otherwise
 */
static int km_vcpu_in_guest(km_vcpu_t* vcpu, void* skip_me)
{
   if (vcpu->state == IN_GUEST && vcpu != skip_me) {
      km_pkill(vcpu, KM_SIGVCPUSTOP);
      km_infox(KM_TRACE_VCPU, "VCPU %d signalled to pause", vcpu->vcpu_id);
      return 1;
   }
   return 0;
}

// Returns 1 if the vcpu is not PAUSED and sends KM_SIGVCPUSTOP to it
static int km_vcpu_not_paused(km_vcpu_t* vcpu, void* skip_me)
{
   if (vcpu->state != PAUSED && vcpu != skip_me) {
      km_pkill(vcpu, KM_SIGVCPUSTOP);
      km_infox(KM_TRACE_VCPU, "VCPU %d signalled to pause", vcpu->vcpu_id);
      return 1;
   }
   return 0;
}

/*
 * Depending on the type, stop IN_GUEST or all vcpus
 */
void km_vcpu_pause_all(km_vcpu_t* vcpu, km_pause_t type)
{
   int count;

   km_mutex_lock(&machine.pause_mtx);
   machine.pause_requested = 1;
   km_mutex_unlock(&machine.pause_mtx);

   switch (type) {
      case GUEST_ONLY:
         for (int i = 0; i < 100 && (count = km_vcpu_apply_all(km_vcpu_in_guest, vcpu)) != 0; i++) {
            nanosleep(&_1ms, NULL);
            km_infox(KM_TRACE_VCPU, "waiting for KVM_RUN to exit - %d", i);
         }
         assert(count == 0);
         return;
      case ALL:
         while (km_vcpu_apply_all(km_vcpu_not_paused, vcpu) != 0) {
            nanosleep(&_1ms, NULL);
            km_infox(KM_TRACE_VCPU, "waiting for VCPUs to pause");
         }
         return;
   }
}

/*
 * Thread completed. Release the resources that we can and mark the vcpu available for reuse by
 * setting vcpu->is_used to 0
 */
void km_vcpu_put(km_vcpu_t* vcpu)
{
   km_mutex_lock(&machine.vm_vcpu_mtx);
   vcpu->guest_thr = 0;
   // vcpu->stack_top = 0; Reused by slist
   vcpu->state = PARKED_IDLE;
   SLIST_INSERT_HEAD(&machine.vm_idle_vcpus.head, vcpu, next_idle);
   if (--machine.vm_vcpu_run_cnt == 0) {
      km_signal_machine_fini();
   }
   km_mutex_unlock(&machine.vm_vcpu_mtx);
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
   km_write_xcrs(vcpu);

   if (km_gdb_client_is_attached() != 0) {
      km_gdb_update_vcpu_debug(vcpu, NULL);
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
   km_vmdriver_clone(vcpu, new_vcpu);
   new_vcpu->regs_valid = 1;

   km_write_registers(new_vcpu);
   km_write_sregisters(new_vcpu);
   km_write_xcrs(vcpu);

   if (km_gdb_client_is_attached() != 0) {
      km_gdb_update_vcpu_debug(new_vcpu, NULL);
   }
   return 0;
}

void km_check_kernel(void)
{
   struct utsname uts;
   int mj, mn;

   /* Try the uname system call.  */
   if (uname(&uts) != 0) {
      km_err(3, "Cannot determine kernel version.");
   }
   if (sscanf(uts.release, "%d.%d", &mj, &mn) != 2) {
      km_errx(3, "Unexpected kernel version format %s", uts.release);
   }
   int oldest_kernel = km_vmdriver_lowest_kernel();
   if (mj * 0x100 + mn < oldest_kernel) {
      km_errx(3,
              "Kernel %s is too old. The oldest supported kernel is %d.%d",
              uts.release,
              km_vmdriver_lowest_kernel() / 0x100,
              km_vmdriver_lowest_kernel() % 0x100);
   }
}

/*
 * Initial steps setting our VM.
 *
 * talk to KVM
 * create VM
 * set VM run memory region
 * prepare cpuid
 *
 * Any failure is fatal, hence void
 */
void km_machine_setup(km_machine_init_params_t* params)
{
   if ((machine.intr_fd = km_internal_eventfd(0, 0)) < 0) {
      km_err(1, "KM: Failed to create machine intr_fd");
   }
   if ((machine.shutdown_fd = km_internal_eventfd(0, 0)) < 0) {
      km_err(1, "KM: Failed to create machine shutdown_fd");
   }
   if (km_machine_init_params.vdev_name != NULL) {   // we were asked for a specific dev name
      if ((machine.kvm_fd = km_internal_open(km_machine_init_params.vdev_name, O_RDWR)) < 0) {
         km_err(1, "KVM: Can't open device file %s", km_machine_init_params.vdev_name);
      }
   } else {
      // default devices, in the order we try to open them
      const_string_t dev_files[] = {DEVICE_KONTAIN, DEVICE_KVM, DEVICE_KKM, NULL};
      for (const_string_t* d = dev_files; *d != NULL; d++) {
         km_infox(KM_TRACE_KVM, "Trying to open device file %s", *d);
         if ((machine.kvm_fd = km_internal_open(*d, O_RDWR)) >= 0) {
            km_machine_init_params.vdev_name = strdup(*d);
            break;
         }
      }
      if (machine.kvm_fd < 0) {
         km_err(1, "Can't open default device file.");
      }
   }
   km_infox(KM_TRACE_KVM, "Using device file %s", km_machine_init_params.vdev_name);

   if (km_vmdriver_get_identity() != KKM_DEVICE_IDENTITY) {
      machine.vm_type = VM_TYPE_KVM;
   } else {
      machine.vm_type = VM_TYPE_KKM;
   }
   km_infox(KM_TRACE_KVM,
            "Setting vm type to %s",
            (machine.vm_type == VM_TYPE_KVM) ? "VM_TYPE_KVM" : "VM_TYPE_KKM");

   km_check_kernel();   // exit there if too old

   int rc;
   if ((rc = ioctl(machine.kvm_fd, KVM_GET_API_VERSION, 0)) < 0) {
      km_err(1, "KVM: get API version failed");
   }
   if (rc != KVM_API_VERSION) {
      km_errx(1, "KVM: API version mismatch");
   }
   if ((machine.mach_fd = km_internal_fd_ioctl(machine.kvm_fd, KVM_CREATE_VM, NULL)) < 0) {
      km_err(1, "KVM: create VM failed");
   }
   if ((machine.vm_run_size = ioctl(machine.kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0)) < 0) {
      km_err(1, "KVM: get VM memory region size failed");
   }
   if (machine.vm_run_size < sizeof(kvm_run_t)) {
      km_errx(1,
              "KVM: suspicious VM memory region size %zu, expecting at least %zu",
              machine.vm_run_size,
              sizeof(kvm_run_t));
   }
   if ((machine.cpuid =
            calloc(1, sizeof(kvm_cpuid2_t) + CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2))) == NULL) {
      km_err(1, "KVM: no memory for CPUID");
   }
   machine.cpuid->nent = CPUID_ENTRIES;
   if (ioctl(machine.kvm_fd, KVM_GET_SUPPORTED_CPUID, machine.cpuid) < 0) {
      km_err(1, "KVM: get supported CPUID failed");
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
       * In the absence of 1gb pages we can only support 2GB, first for text+data, and the second
       * for the stack. See assert() in set_pml4_hierarchy()
       */
      km_infox(KM_TRACE_MEM,
               "KVM: 1gb pages are not supported (pdpe1g=0), setting VM max mem to 2 GiB");
      machine.guest_max_physmem = MIN(2 * GIB, machine.guest_max_physmem);
   }
   if (params->guest_physmem != 0) {
      if ((machine.vm_type == VM_TYPE_KKM) && (params->guest_physmem != GUEST_MAX_PHYSMEM_SUPPORTED)) {
         km_errx(1,
                 "Only %ldGiB physical memory supported with KKM driver",
                 GUEST_MAX_PHYSMEM_SUPPORTED / GIB);
      } else {
         if (params->guest_physmem > machine.guest_max_physmem) {
            km_errx(1,
                    "Cannot set guest memory size to '0x%lx'. Max supported=0x%lx",
                    params->guest_physmem,
                    machine.guest_max_physmem);
         }
         machine.guest_max_physmem = params->guest_physmem;
      }
   }

   /*
    * VM Driver specific initialization
    */
   km_vmdriver_machine_init();   // initialize vmdriver specifics
}

/*
 * initial steps setting our VM
 *
 * talk to KVM
 * create VM
 * set VM run memory region
 * prepare cpuid
 *
 * Any failure is fatal, hence void is returned.
 *
 * If you add code here, consider whether it also needs to be added in km_fork_setup_child_vmstate().
 */
void km_machine_init(km_machine_init_params_t* params)
{
   if (km_called_via_exec() == 1) {
      machine.ppid = km_exec_ppid();
      machine.pid = km_exec_pid();
      machine.next_pid = km_exec_next_pid();
   }
   if (km_fs_init() < 0) {
      km_err(1, "KM: km_fs_init() failed");
   }
   km_machine_setup(params);
   km_mem_init(params);
   km_signal_init();
   km_init_guest_idt();
}
