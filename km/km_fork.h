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

extern void km_forward_sigchild(int signo, siginfo_t* sinfo, void* ucontext_unused);
extern int km_before_fork(km_vcpu_t* vcpu, km_hc_args_t* arg, uint8_t is_clone);
extern int km_dofork(int* in_child);

// pidmap
extern void km_pid_insert(pid_t kontain_pid, pid_t linux_pid);
extern void km_pid_free(pid_t kontain_pid);
extern pid_t km_pid_xlate_kpid(pid_t kontain_pid);
extern pid_t km_pid_xlate_lpid(pid_t linux_pid);
extern pid_t km_newpid(void);

#endif /* !defined(__KM_FORK_H__) */
