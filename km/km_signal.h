/*
 * Copyright © 2019-2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#ifndef __KM_SIGNAL_H__
#define __KM_SIGNAL_H__

#include <signal.h>
#include <sys/signalfd.h>
#include "km.h"

void km_signal_init(void);
void km_signal_fini(void);
void km_post_signal(km_vcpu_t* vcpu, siginfo_t* info);
void km_deliver_signal(km_vcpu_t* vcpu, siginfo_t* info);
void km_deliver_next_signal(km_vcpu_t* vcpu);
int km_dequeue_signal(km_vcpu_t* vcpu, siginfo_t* info);
int km_signal_ready(km_vcpu_t*);
void km_deliver_signal_from_gdb(km_vcpu_t* vcpu, siginfo_t* info);

uint64_t
km_rt_sigprocmask(km_vcpu_t* vcpu, int how, km_sigset_t* set, km_sigset_t* oldset, size_t sigsetsize);
uint64_t
km_rt_sigaction(km_vcpu_t* vcpu, int signo, km_sigaction_t* act, km_sigaction_t* oldact, size_t sigsetsize);
uint64_t km_sigaltstack(km_vcpu_t* vcpu, km_stack_t* new, km_stack_t* old);
void km_rt_sigreturn(km_vcpu_t* vcpu);
uint64_t km_kill(km_vcpu_t* vcpu, pid_t pid, int signo);
uint64_t km_tkill(km_vcpu_t* vcpu, pid_t tid, int signo);
uint64_t km_rt_sigpending(km_vcpu_t* vcpu, km_sigset_t* set, size_t sigsetsize);

void km_wait_for_signal(int sig);
typedef void (*sa_action_t)(int, siginfo_t*, void*);
void km_install_sighandler(int signum, sa_action_t hander_func);
uint64_t km_rt_sigsuspend(km_vcpu_t* vcpu, km_sigset_t* mask, size_t masksize);
uint64_t km_rt_sigtimedwait(km_vcpu_t* vcpu, km_sigset_t* set, siginfo_t* info, struct timespec* timeout, size_t setlen);

static inline int km_sigindex(int signo)
{
   return signo - 1;
}

static inline void km_sigemptyset(km_sigset_t* set)
{
   *set = 0L;
}

static inline void km_sigfillset(km_sigset_t *set)
{
   *set = ~0L;
}

static inline void km_sigaddset(km_sigset_t* set, int signo)
{
   if (signo >= 1 && signo < _NSIG) {
      *set |= (1UL << km_sigindex(signo));
   }
}

static inline void km_sigdelset(km_sigset_t* set, int signo)
{
   if (signo >= 1 && signo < _NSIG) {
      *set &= ~(1UL << km_sigindex(signo));
   }
}

static inline int km_sigismember(const km_sigset_t* set, int signo)
{
   if (signo < 1 && signo >= _NSIG) {
      return 0;
   }
   return (*set & (1UL << km_sigindex(signo))) != 0;
}

/*
 * Called to restore the thread's signal mask after sigsuspend() is done.
 * Call this after the signal that woke up the sigsuspend() hypercall has
 * been delivered.
 * This is km_rt_sigsuspends()'s "destructor".
 */
static inline void km_rt_sigsuspend_revert(km_vcpu_t* vcpu)
{
   if (vcpu->in_sigsuspend != 0) {
      km_infox(KM_TRACE_VCPU,
               "revert save_sigmask 0x%lx to sigmask 0x%lx",
               vcpu->saved_sigmask,
               vcpu->sigmask);
      vcpu->sigmask = vcpu->saved_sigmask;
      vcpu->in_sigsuspend = 0;
   }
}

size_t km_sig_core_notes_length();
size_t km_sig_core_notes_write(char* buf, size_t length);
int km_sig_snapshot_recover(char* buf, size_t length);

#endif /* __KM_SIGNAL_H__ */
