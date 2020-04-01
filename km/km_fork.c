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

#include "km.h"
#include "km_gdb.h"
#include "km_fork.h"


km_fork_state_t km_fork_state;

void km_vm_setup_child_state(km_fork_state_t* threadstate, void *myvcpu)
{
   km_infox(KM_TRACE_HC, "begin");
   // Reinit some fields in machine.  We do not want a structure assignment here.
   machine.kvm_fd = -1;
   machine.mach_fd = -1;
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
            warn("KVM: failed to plug in memory region %d", i);
         }
      }
   }

   km_signal_init();

   /*
    * Now cleanup the passed vcpu and convert it to a vcpu in the child's new vm.
    * The child thread needs the stack it had in the parent and that stack has
    * the address of the vcpu in its frames so we need to keep it.
    */
   km_vcpu_t* vcpu = myvcpu;
   if (km_vcpu_init(vcpu) < 0) {
      abort();
   }

   /*
    * The following is similar to km_vcpu_set_to_run() but we want to use the
    * stack and registers the thread was using in the parent process.  So, we
    * cobble things together for this to work.
    */
   kvm_vcpu_init_sregs(vcpu);
   vcpu->regs = threadstate->regs;
   vcpu->regs_valid = 1;
   km_write_registers(vcpu);
   km_write_sregisters(vcpu);

   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpu_pause_sighandler);
   km_install_sighandler(SIGPIPE, km_forward_fd_signal);
   km_install_sighandler(SIGIO, km_forward_fd_signal);
   km_install_sighandler(SIGCLD, km_forward_sigchild);

   /*
    * Create the thread the rehabilitated vcpu will run in.
    * Tell the new thread to use the existing stack.
    * We may not want all of the state stored in the attribute?
    */
   vcpu->is_active = 1;
   if (pthread_create(&vcpu->vcpu_thread, &km_fork_state.thread_attr, (void* (*)(void*))km_vcpu_run, vcpu) != 0) {
      abort();
   }

   // We are freeing the child's copy of an attr allocated in the parent thread.
   pthread_attr_destroy(&km_fork_state.thread_attr);

   // Now we are ready for ioctl( KVM_RUN ) on this thread.
   // The vcpu thread should be blocked waiting to be set running.
}

/*
 * After a fork we free, discard, cleanup all state that
 * belongs to the parent process that is unusable in the child process.
 * But we do keep some state that describes things from the parent that
 * are being used in the child.
 * Things related to the memory and the open guest fd's.
 * This is modeled after km_machine_fini().
 */
void km_vm_remove_parent_state(km_vcpu_t* myvcpu)
{
   km_infox(KM_TRACE_HC, "begin");
   close(machine.shutdown_fd);
   machine.shutdown_fd = -1;
   km_signal_fini();
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      km_vcpu_t* vcpu;

      if ((vcpu = machine.vm_vcpus[i]) != NULL) {
         if (vcpu == myvcpu) {
            // Preserve some of the forking thread's vcpu in the child.
            if (munmap(vcpu->cpu_run, machine.vm_run_size) != 0) {
               km_err_msg(errno, "munmap vcpu %p cpu_run", vcpu);
            }
            if (close(vcpu->kvm_vcpu_fd) != 0) {
               km_err_msg(errno, "close vcpu %p cpu fd %d", vcpu, vcpu->kvm_vcpu_fd);
            }
         } else {
            km_vcpu_fini(vcpu);
         }
      }
   }

   // Leave vm_mem_regs[] alone we will reuse the info there.

   // Leave the busy and free memory maps since the child thread will be running on the same memory.

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
 * setup and the lone thread in the child set running after the fork() call.
 */
void km_reinit_for_fork_child(void)
{
#if 0
   // Block to let gdb attach to the child process.
   volatile int keep_sleeping = 1;
   while (keep_sleeping != 0) {
      sleep(1);
   }
#endif

   // Disconnect gdb client
   km_gdb_fork_reset();

   // Close the kvm related fd's
   km_vm_remove_parent_state(km_fork_state.vcpu);

   // Create a new vm and a single vcpu for the thread that survives the fork.
   km_vm_setup_child_state(&km_fork_state, km_fork_state.vcpu);

   /*
    * TODO: We need to wait for the new vcpu thread to startup and pause itself before
    * resuming it below.
    */

   // Get the child's only vcpu going
   km_vcpu_resume_all();
}

/*
 * Perform the fork().
 * returns:
 *   0 - the caller is the child process
 *   > 0 - the caller is the parent and the fork succeeded
 *   < 0 - the caller is the parent and the fork failed.
 *         There is no child process.
 */
int km_dofork(void)
{
   int rc;
   km_fork_state.arg->hc_ret = rc = fork();
   if (rc == 0) {
      // Set the child's km pid immediately so km_info() reports correct process id.
      machine.pid = km_fork_state.child_pid;
   }
   km_info(KM_TRACE_HC, "fork() returns %d, child_pid %d", rc, km_fork_state.child_pid);
   if (rc < 0) {              // parent process, fork failed
      km_fork_state.arg->hc_ret = -errno;
      pthread_attr_destroy(&km_fork_state.thread_attr);
      km_fork_state.fork_in_progress = 0;
      km_vcpu_resume_all();
   } else if (rc > 0) {       // parent process, fork succeeded
      km_pid_insert(km_fork_state.child_pid, rc);
      km_fork_state.arg->hc_ret = km_fork_state.child_pid;
      pthread_attr_destroy(&km_fork_state.thread_attr);
      km_fork_state.fork_in_progress = 0;
      km_vcpu_resume_all();
   }
   return rc;
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

/*
 * This small group of code maps between km pids and their associated linux pids.
 * This is here so that the various wait() hypercalls can map a km pid into a linux
 * pid where needed before calling the kernel's wait() system call.
 * We also linux pids returned by wait back into km pid's.
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
   static pid_t newpid = 100;

   return ++newpid;
}
