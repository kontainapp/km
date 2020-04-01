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

#ifndef __KM_FORK_H__
#define __KM_FORK_H__

/*
 * TODO: fix these data structures so that multiple threads can be performing
 * fork() hypercalls concurrently.
 */

/*
 * State from the parent process thread that needs to be present in the
 * thread in the child process.  These are things that we won't be able
 * to find if it is stored in the stack of the forking thread.
 * Add stuff as we learn more.
 */
typedef struct km_fork_state {
   int fork_in_progress;
   km_gva_t stack_top;
   km_gva_t guest_thr;
   pid_t child_pid;
   km_hc_args_t* arg;            // fork hypercall args so that the return code can be set
   km_vcpu_t* vcpu;
   kvm_regs_t regs;
   pthread_attr_t thread_attr;   // remember the forking threads stackaddr and stacksize
} km_fork_state_t;

extern km_fork_state_t km_fork_state;

extern void km_vm_setup_child_state(km_fork_state_t* threadstate, void *myvcpu);
extern void km_reinit_for_fork_child(void);
extern int km_dofork(void);
extern void km_vm_remove_parent_state(km_vcpu_t* myvcpu);
extern void km_forward_sigchild(int signo, siginfo_t* sinfo, void* ucontext_unused);

// pidmap
extern void km_pid_insert(pid_t kontain_pid, pid_t linux_pid);
extern void km_pid_free(pid_t kontain_pid);
extern pid_t km_pid_xlate_kpid(pid_t kontain_pid);
extern pid_t km_pid_xlate_lpid(pid_t linux_pid);
extern pid_t km_newpid(void);

#endif /* !defined(__KM_FORK_H__) */
