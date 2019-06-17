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
 *
 * Signal-related wrappers for KM threads/KVM vcpu runs.
 */

#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_mem.h"
#include "km_signal.h"

void km_block_signal(int signum)
{
   sigset_t signal_set;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
}

void km_install_sighandler(int signum, sa_handler_t func)
{
   struct sigaction sa = {.sa_handler = func, .sa_flags = 0};

   sigemptyset(&sa.sa_mask);
   if (sigaction(signum, &sa, NULL) < 0) {
      err(1, "Failed to set handler for signal %d", signum);
   }
}

/*
 * Make current thread block until signal is sent to it via pthread_kill().
 * Note that for the thread to not die on the signal it also needs
 * to block it, so to use in multi-threaded code other threads (or parent)
 * needs to block it or handle it too.
 */
void km_wait_for_signal(int signum)
{
   sigset_t signal_set;
   sigset_t old_signal_set;
   int received_signal;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, &old_signal_set);
   sigwait(&signal_set, &received_signal);
   pthread_sigmask(SIG_SETMASK, &old_signal_set, NULL);
}

#define NSIGENTRY 8
static km_signal_t signal_entries[NSIGENTRY];

/*
 * Signal classification sets.
 * Source: https://www.gnu.org/software/libc/manual/html_node/Standard-Signals.html#Standard-Signals
 *
 * Note: most of these sigsets aren't used by the code yet, but I'm pretty sure
 *       they will come in handy later.
 */
static km_sigset_t perror_signals = 0;   // Program errors
static km_sigset_t term_signals = 0;
static km_sigset_t alarm_signals = 0;
static km_sigset_t aio_signals = 0;
static km_sigset_t jc_signals = 0;
static km_sigset_t oerror_signals = 0;
static km_sigset_t misc_signals = 0;

void km_signal_init(void)
{
   for (int i = 0; i < NSIGENTRY; i++) {
      TAILQ_INSERT_TAIL(&machine.sigfree.head, &signal_entries[i], link);
   }

   // program error signals
   km_sigemptyset(&perror_signals);
   km_sigaddset(&perror_signals, SIGFPE);
   km_sigaddset(&perror_signals, SIGILL);
   km_sigaddset(&perror_signals, SIGSEGV);
   km_sigaddset(&perror_signals, SIGBUS);
   km_sigaddset(&perror_signals, SIGABRT);
   km_sigaddset(&perror_signals, SIGIOT);
   km_sigaddset(&perror_signals, SIGTRAP);
   km_sigaddset(&perror_signals, SIGSYS);

   // terminaltion signals
   km_sigemptyset(&term_signals);
   km_sigaddset(&term_signals, SIGTERM);
   km_sigaddset(&term_signals, SIGINT);
   km_sigaddset(&term_signals, SIGQUIT);   // Special case. Drops a core
   km_sigaddset(&term_signals, SIGKILL);
   km_sigaddset(&term_signals, SIGHUP);

   // alarm signals
   km_sigemptyset(&alarm_signals);
   km_sigaddset(&alarm_signals, SIGALRM);
   km_sigaddset(&alarm_signals, SIGVTALRM);
   km_sigaddset(&alarm_signals, SIGPROF);

   // async IO signals
   km_sigemptyset(&aio_signals);
   km_sigaddset(&aio_signals, SIGIO);
   km_sigaddset(&aio_signals, SIGURG);
   km_sigaddset(&aio_signals, SIGPOLL);

   // Job Control signals
   km_sigemptyset(&jc_signals);
   km_sigaddset(&jc_signals, SIGCHLD);
   km_sigaddset(&jc_signals, SIGCONT);
   km_sigaddset(&jc_signals, SIGSTOP);
   km_sigaddset(&jc_signals, SIGTTIN);
   km_sigaddset(&jc_signals, SIGTTOU);

   // Operation Error km_signals
   km_sigemptyset(&oerror_signals);
   km_sigaddset(&oerror_signals, SIGPIPE);
   km_sigaddset(&oerror_signals, SIGXCPU);
   km_sigaddset(&oerror_signals, SIGXFSZ);

   // Miscalaneous km_signals
   km_sigemptyset(&misc_signals);
   km_sigaddset(&misc_signals, SIGUSR1);
   km_sigaddset(&misc_signals, SIGUSR2);
   km_sigaddset(&misc_signals, SIGWINCH);
}

void km_signal_fini(void)
{
}

static inline void enqueue_signal(km_signal_list_t* slist, siginfo_t* info)
{
   km_signal_t* sig;

   km_signal_lock();
   if ((sig = TAILQ_FIRST(&machine.sigfree.head)) == NULL) {
      km_signal_unlock();
      err(1, "No free signal entries");
   }
   TAILQ_REMOVE(&machine.sigfree.head, sig, link);
   sig->info = *info;
   TAILQ_INSERT_TAIL(&slist->head, sig, link);
   km_signal_unlock();
}

static inline int sigpri(int signo)
{
   // program error signals come first
   if (km_sigismember(&perror_signals, signo)) {
      return 0;
   }
   return -signo;
}

static inline int dequeue_signal(km_signal_list_t* slist, km_sigset_t* blocked, siginfo_t* info)
{
   int rc = 0;
   km_signal_t* chosen = NULL;
   km_signal_t* sig = NULL;

   km_signal_lock();
   TAILQ_FOREACH (sig, &slist->head, link) {
      if (km_sigismember(blocked, sig->info.si_signo)) {
         continue;
      }
      if (chosen == NULL || sigpri(sig->info.si_signo) > sigpri(chosen->info.si_signo)) {
         chosen = sig;
      }
   }
   if (chosen != NULL) {
      TAILQ_REMOVE(&slist->head, chosen, link);
      *info = chosen->info;
      rc = 1;
      TAILQ_INSERT_TAIL(&machine.sigfree.head, chosen, link);
   }
   km_signal_unlock();
   return rc;
}

static inline void get_pending_signals(km_vcpu_t* vcpu, km_sigset_t* set)
{
   km_signal_t* sig;

   km_sigemptyset(set);
   km_signal_lock();
   TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
      km_sigaddset(set, sig->info.si_signo);
   }
   TAILQ_FOREACH (sig, &machine.sigpending.head, link) {
      km_sigaddset(set, sig->info.si_signo);
   }
   km_signal_unlock();
}

static inline int next_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   if (dequeue_signal(&vcpu->sigpending, &vcpu->sigmask, info) != 0) {
      return 1;
   }
   if (dequeue_signal(&machine.sigpending, &vcpu->sigmask, info) != 0) {
      return 1;
   }
   return 0;
}

void km_post_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   km_signal_list_t* slist = (vcpu == NULL) ? &machine.sigpending : &vcpu->sigpending;
   enqueue_signal(slist, info);
}

#define RED_ZONE (128)

static inline void do_guest_handler(km_vcpu_t* vcpu, siginfo_t* info, km_sigaction_t* act)
{
   km_read_registers(vcpu);

   km_gva_t si_gva = vcpu->regs.rsp - RED_ZONE - sizeof(siginfo_t);
   km_gva_t newsp_gva = si_gva - sizeof(vcpu->regs);

   // TODO: Check that newsp_gva is valid memory.

   memcpy(km_gva_to_kma(si_gva), info, sizeof(siginfo_t));
   void* newsp = km_gva_to_kma(newsp_gva);
   memcpy(newsp, &vcpu->regs, sizeof(vcpu->regs));
   vcpu->regs.rsp = newsp_gva;
   vcpu->regs.rip = km_guest.km_sighandle;
   vcpu->regs.rdi = newsp_gva;
   vcpu->regs.rsi = act->handler;
   vcpu->regs.rdx = info->si_signo;
   vcpu->regs.rcx = si_gva;
   vcpu->regs.r8 = 0;   // TODO: ucontext

   km_write_registers(vcpu);
}

/*
 * Deliver signal to guest. What delivery looks like depends on the signal disposition.
 * Ignored signals are ignored.
 * Default handler signals typically terminate, possobly with a core,
 * Handled signals result in the guest being setup to run the signal handler
 * on the next VM_RUN call.
 */
void km_deliver_signal(km_vcpu_t* vcpu)
{
   siginfo_t info;

   if (!next_signal(vcpu, &info)) {
      return;
   }

   km_sigaction_t* act = &machine.sigactions[km_sigindex(info.si_signo)];
   if (act->handler == (km_gva_t)SIG_IGN) {
      return;
   }
   if (act->handler == (km_gva_t)SIG_DFL) {
      int core_dumped = 0;
      // Signal default action is terminate the process. Some signals need a core dump too.
      vcpu->is_paused = 1;
      machine.pause_requested = 1;
      km_vcpu_apply_all(km_vcpu_pause, 0);
      km_vcpu_wait_for_all_to_pause();
      if (km_sigismember(&perror_signals, info.si_signo) || info.si_signo == SIGQUIT) {
         km_dump_core(vcpu, NULL);
         core_dumped = 1;
      }
      errx(info.si_signo, "guest: %s %s", strsignal(info.si_signo), (core_dumped) ? "(core dumped)" : "");
   }

   assert(act->handler != (km_gva_t)SIG_IGN);
   do_guest_handler(vcpu, &info, act);
}

uint64_t
km_rt_sigprocmask(km_vcpu_t* vcpu, int how, km_sigset_t* set, km_sigset_t* oldset, size_t sigsetsize)
{
   /*
    * No locking required here because this syscall will only change the mask for the current
    * thread. Single threaded by nature.
    */
   if (oldset != NULL) {
      *oldset = vcpu->sigmask;
   }
   if (set != NULL) {
      switch (how) {
         case SIG_BLOCK:
            for (int signo = 1; signo < _NSIG; signo++) {
               if (km_sigismember(set, signo)) {
                  km_sigaddset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_UNBLOCK:
            for (int signo = 1; signo < _NSIG; signo++) {
               if (km_sigismember(set, signo)) {
                  km_sigdelset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_SETMASK:
            vcpu->sigmask = *set;
            break;
      }
   }
   return 0;
}

uint64_t
km_rt_sigaction(km_vcpu_t* vcpu, int signo, km_sigaction_t* act, km_sigaction_t* oldact, size_t sigsetsize)
{
   if (signo < 1 || signo >= NSIG) {
      return EINVAL;
   }
   /*
    * sigactions are process-wide, so need the lock.
    */
   km_signal_lock();
   if (oldact != NULL) {
      *oldact = machine.sigactions[km_sigindex(signo)];
   }
   if (act != NULL) {
      machine.sigactions[km_sigindex(signo)] = *act;
      if (act->handler == (km_gva_t)SIG_IGN) {
         // TODO: Purge pending signals on ignore.
      }
   }
   km_signal_unlock();
   return 0;
}

uint64_t km_rt_sigreturn(km_vcpu_t* vcpu, void* savregs)
{
   km_read_registers(vcpu);
   memcpy(&vcpu->regs, savregs, sizeof(vcpu->regs));
   vcpu->regs.rip++;
   km_write_registers(vcpu);
   return 0;
}

uint64_t km_kill(km_vcpu_t* vcpu, pid_t pid, int signo)
{
   if (pid != 0) {
      return EINVAL;
   }
   if (signo < 1 || signo >= NSIG) {
      return EINVAL;
   }

   // Untargeted signal.
   siginfo_t info = {.si_signo = signo, .si_code = SI_USER};
   km_post_signal(NULL, &info);
   return 0;
}

uint64_t km_rt_sigpending(km_vcpu_t* vcpu, km_sigset_t* set, size_t sigsetsize)
{
   get_pending_signals(vcpu, set);
   return 0;
}