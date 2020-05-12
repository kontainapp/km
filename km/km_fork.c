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

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#include "km.h"
#include "km_mem.h"
#include "km_gdb.h"
#include "km_fork.h"

/*
 * fork() or clone() state from the parent process thread that needs to be present in the thread in
 * the child process.  These are things that we won't be able to find if it is stored in the stack
 * of the forking thread.
 */
typedef struct km_fork_state {
   pthread_mutex_t mutex;
   pthread_cond_t cond;          // to serialize concurrent fork requests
   uint8_t is_clone;             // if true, do a clone() hypercall, else fork()
   uint8_t fork_in_progress;
   km_hc_args_t* arg;
   pid_t km_parent_pid;          // kontain pid
   pid_t km_child_pid;           // kontain pid
   kvm_regs_t regs;
   km_gva_t stack_top;
   km_gva_t guest_thr;
   km_stack_t sigaltstack;
} km_fork_state_t;

static km_fork_state_t km_fork_state = {
   .mutex = PTHREAD_MUTEX_INITIALIZER,
   .fork_in_progress = 0,
};

/*
 * This function is modeled after km_machine_init(). There are differences
 * because after a fork or clone hypercall we want to use some of the state
 * inherited from the parent process. We get all of the memory from the parent
 * so we don't need to initialize the memory maps, but since we must destroy this
 * process' references to the parents kvm related fd's we need to recreate the
 * memory regions that kvm knows about.  Then we need to create a vcpu for the
 * payload thread that survives the fork operation.  Plus we must transplant
 * the stack, tls, and alternate signal stack from that thread's vcpu into the
 * fresh vcpu. The signal handlers are instact after a fork/clone so we don't
 * need to reinitialize those, but, I think we do.
 * If you add code to this function consider whether similar code needs to be
 * added to km_machine_init().
 */
static void km_fork_setup_child_vmstate(void)
{
   km_infox(KM_TRACE_FORK, "begin");

   // Reinit some fields in machine.  We do not want a structure assignment here.
   machine.brk_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   machine.signal_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   TAILQ_INIT(&machine.sigpending.head);
   TAILQ_INIT(&machine.sigfree.head);
   machine.mmaps.mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   machine.pause_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
   machine.pause_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;

   // No need to call km_hcalls_init().  It's all setup from the parent process.
   // No need to call km_fs_init().  We use the guest fd <--> host fd maps from the parent.

   km_machine_setup(&km_machine_init_params);

   /*
    * No need to call km_mem_init(). we use the memslots and busy/free lists as they are in the parent.
    * We just need to tell kvm how the memory looks.
    */
   for (int i = 0; i < KM_MEM_SLOTS; i++) {
      if (machine.vm_mem_regs[i].memory_size != 0) {
         if (ioctl(machine.mach_fd, KVM_SET_USER_MEMORY_REGION, &machine.vm_mem_regs[i]) < 0) {
            err(2, "KVM: failed to plug in memory region %d", i);
         }
      }
   }

   km_signal_init();             // initalize signal wait queue and the signal entry free list

   // No need to call km_init_guest_idt(). Use what was inherited from the parent process.

   km_vcpu_t* vcpu = km_vcpu_get();    // Get a new vcpu for the child's only thread.
   if (vcpu == NULL) {
      err(2, "couldn't get a vcpu");
   }
   vcpu->stack_top = km_fork_state.stack_top;
   vcpu->guest_thr = km_fork_state.guest_thr;
   vcpu->sigaltstack = km_fork_state.sigaltstack;

   /*
    * The following is similar to km_vcpu_set_to_run() but we want to use the stack and registers
    * the thread was using in the parent process.  So, we cobble things together for this to work.
    */
   km_infox(KM_TRACE_VCPU, "setting up child vcpu: rip 0x%llx, rsp 0x%llx", km_fork_state.regs.rip, km_fork_state.regs.rsp);
   kvm_vcpu_init_sregs(vcpu);
   vcpu->regs = km_fork_state.regs;
   vcpu->regs_valid = 1;
   km_write_registers(vcpu);
   km_write_sregisters(vcpu);
}

/*
 * After a fork we free, discard, cleanup all state that belongs to the parent process that is
 * unusable in the child process. But we do keep some state that describes things from the parent
 * that are being used in the child.
 * Things related to the memory and the open guest fd's.
 * This is modeled after km_machine_fini().
 */
static void km_fork_remove_parent_vmstate(void)
{
   km_infox(KM_TRACE_HC, "begin");
   close(machine.shutdown_fd);
   machine.shutdown_fd = -1;
   km_signal_fini();

   // Should try to free all of the stacks for the now defunt vcpu threads?
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      if (machine.vm_vcpus[i] != NULL) {
         km_vcpu_fini(machine.vm_vcpus[i]);
         machine.vm_vcpus[i] = NULL;
      }
   }

   // Leave machine.vm_mem_regs[] alone we will reuse the info there.

   // Leave the busy and free memory maps since the child process will be running on the same memory.

   // Close these fd's since they belong to the parent.
   if (machine.kvm_fd >= 0) {
      close(machine.kvm_fd);
      machine.kvm_fd = -1;
   }
   if (machine.cpuid != NULL) {
      free(machine.cpuid);
      machine.cpuid = NULL;
   }
   if (machine.mach_fd >= 0) {
      close(machine.mach_fd);
      machine.mach_fd = -1;
   }
   if (machine.intr_fd >= 0) {
      close(machine.intr_fd);
      machine.intr_fd = -1;
   }

   // We preserve the hostfd <--> guestfd maps in the child
}

/*
 * Called from the child process after the fork happens to have the vm and vcpu
 * setup and the lone payload thread in the child created.
 * The caller must start the vcpu running.
 */
static void km_fork_child_vm_init(void)
{
   // Disconnect gdb client
   km_gdb_fork_reset();

   // TODO: Conditionally block to let gdb attach to the child km process before we get going.

   // Close the kvm related fd's.  These are from the parent process.
   km_fork_remove_parent_vmstate();

   // Create a new vm and a single vcpu for the payload thread that survives the fork.
   km_fork_setup_child_vmstate();

   km_start_vcpus();     // get the payload going
}

/*
 * fork() or clone() prefix function.
 */
int km_before_fork(km_vcpu_t* vcpu, km_hc_args_t* arg, uint8_t is_clone)
{
   km_mutex_lock(&km_fork_state.mutex);
   while (km_fork_state.fork_in_progress != 0) {
      km_cond_wait(&km_fork_state.cond, &km_fork_state.mutex);
   }

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &km_fork_state.regs) < 0) {
      km_mutex_unlock(&km_fork_state.mutex);
      return -errno;
   }
   km_fork_state.fork_in_progress = 1;
   km_fork_state.is_clone = is_clone;
   km_fork_state.km_parent_pid = machine.pid;
   km_fork_state.km_child_pid = km_newpid();
   km_fork_state.arg = arg;
   km_fork_state.stack_top = vcpu->stack_top;
   km_fork_state.guest_thr = vcpu->guest_thr;
   km_fork_state.sigaltstack = vcpu->sigaltstack;

   km_mutex_unlock(&km_fork_state.mutex);
   return 0;
}

/*
 * If needed, perform a fork() or clone() system call.
 * Call this from km's main() thread.  We want the
 * km main thread to be the surviving thread in the child
 * process since it is involved with payload termination
 * and the gdb server runs in the main thread context.
 * Returns:
 *   0 - fork or clone was not requested
 *   1 - fork or clone requested and performed
 * in_child - returns to the caller whether we are returning in the
 *   child process or the parent process.
 */
int km_dofork(int* in_child)
{
   pid_t linux_child_pid;

   if (in_child != NULL) {
      *in_child = 0;
   }
   km_infox(KM_TRACE_FORK, "fork_in_progress %d, is_clone %d", km_fork_state.fork_in_progress, km_fork_state.is_clone);
   km_mutex_lock(&km_fork_state.mutex);
   if (km_fork_state.fork_in_progress == 0) {
      km_mutex_unlock(&km_fork_state.mutex);
      return 0;
   }

   // We need to block more signals here, this just gets the simple test passing.
   sigset_t blockthese;
   sigset_t formermask;
   sigemptyset(&blockthese);
   sigaddset(&blockthese, SIGUSR1);
   int rc = sigprocmask(SIG_BLOCK, &blockthese, &formermask);
   assert(rc == 0);

   km_trace_include_pid(1);       // include the pid in trace output
   if (km_fork_state.is_clone != 0) {
      km_hc_args_t* arg = km_fork_state.arg;
      linux_child_pid = syscall(SYS_clone, arg->arg1, arg->arg2, arg->arg3, (uint64_t)km_gva_to_kma(arg->arg4), arg->arg5);
   } else {
      linux_child_pid = fork();
   }
   if (linux_child_pid == 0) {   // this is the child process
      
      // Set the child's km pid immediately so km_info() reports correct process id.
      machine.pid = km_fork_state.km_child_pid;
      km_infox(KM_TRACE_FORK, "child: after fork/clone");
      machine.ppid = km_fork_state.km_parent_pid;
      km_fork_state.fork_in_progress = 0;
      km_fork_state.mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
      km_fork_state.cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
      km_fork_state.arg->hc_ret = 0;

      km_fork_child_vm_init();           // create the vm and a single vcpu for the child payload thread
      if (in_child != NULL) {
         *in_child = 1;
      }
   } else { // We are the parent process here.
      km_infox(KM_TRACE_FORK, "parent: after fork/clone linux_child_pid %d, errno %d", linux_child_pid, errno);
      if (linux_child_pid >= 0) {
         km_pid_insert(km_fork_state.km_child_pid, linux_child_pid);
         km_fork_state.arg->hc_ret = km_fork_state.km_child_pid;
      } else {  // fork failed
         km_fork_state.arg->hc_ret = -errno;
      }
      km_fork_state.fork_in_progress = 0;
      km_cond_signal(&km_fork_state.cond);
      km_mutex_unlock(&km_fork_state.mutex);
   }

   // fork/clone is done, unblock signals.
   rc = sigprocmask(SIG_SETMASK, &formermask, NULL);
   assert(rc == 0);

   km_vcpu_resume_all();    // let the vcpu's get back to work
   return 1;
}

/*
 * Receives control when one of the km instances forked from this instance
 * exits.  We forward the signal on to the payload in this instance of km.
 */
void km_forward_sigchild(int signo, siginfo_t* sinfo, void* ucontext_unused)
{
   siginfo_t info = *sinfo;

   info.si_pid = km_pid_xlate_lpid(sinfo->si_pid);
   km_post_signal(NULL, &info);
}

// TODO: pidmap belongs in its own source file.

/*
 * This small group of code maps between km pids and their associated linux pids.
 * This is here so that the various wait() hypercalls can map a km pid into a linux
 * pid where needed before calling the kernel's wait() system call.
 * We also convert linux pids returned by wait back into km pid's.
 * And this is because the linux kernel has no knowledge of the km pids.
 */
typedef struct km_linux_kontain_pidmap {
   pid_t kontain_pid;
   pid_t linux_pid;
} km_linux_kontain_pidmap_t;
#define KM_MAX_PID_SLOTS 128
#define KM_INVALID_PID 0
km_linux_kontain_pidmap_t km_lk_pidmap[KM_MAX_PID_SLOTS];

void km_pid_insert(pid_t kontain_pid, pid_t linux_pid)
{
   // Find an empty slot
   for (int i = 0; i < KM_MAX_PID_SLOTS; i++) {
      if (km_lk_pidmap[i].kontain_pid == KM_INVALID_PID) {
         km_lk_pidmap[i].kontain_pid = kontain_pid;
         km_lk_pidmap[i].linux_pid = linux_pid;
         return;
      }
      // TODO check for duplicate entries
   }
   errx(2, "Too many kontain to linux pid map entries");
}

km_linux_kontain_pidmap_t* km_pid_find_lpid(pid_t linux_pid)
{
   for (int i = 0; i < KM_MAX_PID_SLOTS; i++) {
      if (km_lk_pidmap[i].linux_pid == linux_pid) {
         return &km_lk_pidmap[i];
      }
   }
   return NULL;
}

void km_pid_slot_free(km_linux_kontain_pidmap_t* pme)
{
   pme->kontain_pid = KM_INVALID_PID;
   pme->linux_pid = KM_INVALID_PID;
}

void km_pid_free(pid_t kontain_pid)
{
   for (int i = 0; i < KM_MAX_PID_SLOTS; i++) {
      if (km_lk_pidmap[i].kontain_pid == kontain_pid) {
         km_pid_slot_free(&km_lk_pidmap[i]);
         return;
      }
   }
   assert("Freeing unknown pid" == NULL);
}

pid_t km_pid_xlate_kpid(pid_t kontain_pid)
{
   for (int i = 0; i < KM_MAX_PID_SLOTS; i++) {
      if (km_lk_pidmap[i].kontain_pid == kontain_pid) {
         return km_lk_pidmap[i].linux_pid;
      }
   }
   // Didn't find that linux pid
   km_infox(KM_TRACE_HC, "Couldn't map kontain pid %d to a linux pid", kontain_pid);
   return kontain_pid;
}

pid_t km_pid_xlate_lpid(pid_t linux_pid)
{
   for (int i = 0; i < KM_MAX_PID_SLOTS; i++) {
      if (km_lk_pidmap[i].linux_pid == linux_pid) {
         return km_lk_pidmap[i].kontain_pid;
      }
   }
   // Didn't find that linux pid
   km_infox(KM_TRACE_HC, "Couldn't map linux pid %d to a kontain pid", linux_pid);
   return linux_pid;
}

// Placeholder for the pid allocator function
pid_t km_newpid(void)
{
   return ++machine.next_pid;
}
