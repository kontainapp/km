/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
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

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_filesys.h"
#include "km_fork.h"
#include "km_guest.h"
#include "km_hcalls.h"
#include "km_kkm.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"

// SA_RESTORER is GNU/Linux i386/amd64 specific.
#ifndef SA_RESTORER
#define SA_RESTORER 0x4000000
#endif

/*
 * Threads that wait for a signal's arrive are enqueued on the km_signal_wait_queue until the signal
 * arrives.  Each vcpu has its own condition variable that it waits on.
 */
TAILQ_HEAD(km_signal_wait_queue, km_vcpu);
struct km_signal_wait_queue km_signal_wait_queue;

void km_install_sighandler(int signum, sa_action_t func)
{
   struct sigaction sa = {.sa_sigaction = func, .sa_flags = SA_SIGINFO};

   sigemptyset(&sa.sa_mask);
   if (sigaction(signum, &sa, NULL) < 0) {
      km_err(1, "Failed to set handler for signal %d", signum);
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
   km_sigmask(SIG_BLOCK, &signal_set, &old_signal_set);
   sigwait(&signal_set, &received_signal);
   km_sigmask(SIG_SETMASK, &old_signal_set, NULL);
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

/*
 * signals that ignore blocking if generated by CPU fault
 * (ie. si_code == SI_KERNEL).
 * See NOTES in 'man 2 sigprocmask'. Man page says behavior is undefined.
 * We will define it to mean 'we don't ignore'.
 */
static km_sigset_t ign_block_signals = 0;
static km_sigset_t no_catch_signals = 0;   // signals that can't be caught, blocked, or ignored
static km_sigset_t def_ign_signals = 0;    // default ignore signals

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

   // Miscellaneous km_signals
   km_sigemptyset(&misc_signals);
   km_sigaddset(&misc_signals, SIGUSR1);
   km_sigaddset(&misc_signals, SIGUSR2);
   km_sigaddset(&misc_signals, SIGWINCH);

   km_sigemptyset(&ign_block_signals);
   km_sigaddset(&ign_block_signals, SIGBUS);
   km_sigaddset(&ign_block_signals, SIGFPE);
   km_sigaddset(&ign_block_signals, SIGILL);
   km_sigaddset(&ign_block_signals, SIGSEGV);
   /*
    * signals that cannot be caught, blocked, or ignored
    */
   km_sigemptyset(&no_catch_signals);
   km_sigaddset(&no_catch_signals, SIGKILL);
   km_sigaddset(&no_catch_signals, SIGSTOP);

   /*
    * Signals whose default action is ignore.
    * Source: http://man7.org/linux/man-pages/man7/signal.7.html
    */
   km_sigemptyset(&def_ign_signals);
   km_sigaddset(&def_ign_signals, SIGCHLD);
   km_sigaddset(&def_ign_signals, SIGURG);
   km_sigaddset(&def_ign_signals, SIGWINCH);

   TAILQ_INIT(&km_signal_wait_queue);
}

void km_signal_fini(void)
{
}

static inline void enqueue_signal_nolock(km_signal_list_t* slist, siginfo_t* info)
{
   km_signal_t* sig;

   if ((sig = TAILQ_FIRST(&machine.sigfree.head)) == NULL) {
      km_err(1, "No free signal entries");
   }
   TAILQ_REMOVE(&machine.sigfree.head, sig, link);
   sig->info = *info;
   TAILQ_INSERT_TAIL(&slist->head, sig, link);
}

static inline void enqueue_signal(km_signal_list_t* slist, siginfo_t* info)
{
   km_signal_lock();
   enqueue_signal_nolock(slist, info);
   km_signal_unlock();
}

static inline int sigpri(int signo)
{
   // program error signals come first
   if (km_sigismember(&perror_signals, signo) != 0) {
      return 0;
   }
   return -signo;
}

static int is_blocked(km_sigset_t* blocked, siginfo_t* info)
{
   if (km_sigismember(blocked, info->si_signo) != 0) {
      /*
       * If this signal was generated by CPU fault and it is one of the
       * SIGILL, SIGFPE, SIGSEGV, or SIGBUS, then ignore the mask and
       * process it now.
       */
      if ((km_sigismember(&ign_block_signals, info->si_signo) == 0) || (info->si_code != SI_KERNEL)) {
         return 1;
      }
   }
   return 0;
}

static inline int dequeue_signal(km_signal_list_t* slist, km_sigset_t* blocked, siginfo_t* info)
{
   int rc = 0;
   km_signal_t* chosen = NULL;
   km_signal_t* sig = NULL;

   TAILQ_FOREACH (sig, &slist->head, link) {
      if (is_blocked(blocked, &sig->info) != 0) {
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

/*
 * Dequeue signal, if any, for processing. The signal is either delivered right away, or sent to gdb
 * client to examine and allow to pass. If the gdb client decides to allow the signal it will
 * instruct the gdb server to deliver the signal.
 * Return 1 and initialize info if there is a signals to deliver, 0 otherwise.
 */
int km_dequeue_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   km_signal_lock();
   if (dequeue_signal(&vcpu->sigpending, &vcpu->sigmask, info) != 0) {
      km_signal_unlock();
      return 1;
   }
   if (dequeue_signal(&machine.sigpending, &vcpu->sigmask, info) != 0) {
      km_signal_unlock();
      return 1;
   }
   km_signal_unlock();
   return 0;
}

/*
 * Return the number of the next unblocked signal for the passed vcpu.
 * If there are no pending signals, return 0.
 * The caller must have acquired the signal lock.
 */
static int km_signal_ready_nolock(km_vcpu_t* vcpu)
{
   km_signal_t* sig;
   km_signal_t* next_sig;

   TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
      if (is_blocked(&vcpu->sigmask, &sig->info) == 0) {
         km_infox(KM_TRACE_VCPU, "vcpu %d signal %d ready", vcpu->vcpu_id, sig->info.si_signo);
         return sig->info.si_signo;
      }
   }
   TAILQ_FOREACH_SAFE (sig, &machine.sigpending.head, link, next_sig) {
      if (is_blocked(&vcpu->sigmask, &sig->info) == 0) {
         // A process-wide signal can only be claimed by one thread.
         TAILQ_REMOVE(&machine.sigpending.head, sig, link);
         TAILQ_INSERT_TAIL(&vcpu->sigpending.head, sig, link);
         km_infox(KM_TRACE_VCPU, "VM signal %d ready", sig->info.si_signo);
         return sig->info.si_signo;
      }
   }
   return 0;
}

/*
 * Return the number of the next unblocked signal for the passed vcpu.
 * If there are no pending signals, return 0.
 *
 * This only used for gdb stub to report thread status. Logic here is the same as km_dequeue_signal()
 * above, except here it doesn't dequeue, and it doesn't search for higher priority signal.
 */
int km_signal_ready(km_vcpu_t* vcpu)
{
   km_signal_lock();
   int signo = km_signal_ready_nolock(vcpu);
   km_signal_unlock();
   return signo;
}

/*
 * Determine whether this signal already pending.
 */
static inline int signal_pending(km_vcpu_t* vcpu, siginfo_t* info)
{
   km_signal_t* sig;

   km_signal_lock();
   if (vcpu != NULL) {
      TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
         if (sig->info.si_signo == info->si_signo) {
            km_signal_unlock();
            return 1;
         }
      }
   }
   TAILQ_FOREACH (sig, &machine.sigpending.head, link) {
      if (sig->info.si_signo == info->si_signo) {
         km_signal_unlock();
         return 1;
      }
   }
   km_signal_unlock();
   return 0;
}

/*
 * Look for a vcpu blocked in sigsuspend() that is waiting for the passed signal. Enqueue the
 * signal on the first found thread's pending queue and wake that thread.
 * Returns:
 *   0 - no thread was woken
 *   1 - we found a thread to take the signal and woke the thread
 */
int km_wakeup_suspended_thread(siginfo_t* info)
{
   km_vcpu_t* vcpu;
   int wake_count = 0;

   km_signal_lock();
   TAILQ_FOREACH (vcpu, &km_signal_wait_queue, signal_link) {
      if (km_sigismember(&vcpu->sigmask, info->si_signo) ==
          0) {   // this signal is not blocked on this vcpu
         km_infox(KM_TRACE_VCPU,
                  "Waking sigsuspend()'ed vcpu %d with signal %d",
                  vcpu->vcpu_id,
                  info->si_signo);
         TAILQ_REMOVE(&km_signal_wait_queue, vcpu, signal_link);
         enqueue_signal_nolock(&vcpu->sigpending, info);
         km_cond_signal(&vcpu->signal_wait_cv);
         wake_count++;
         break;
      }
   }
   km_signal_unlock();

   return wake_count;
}

void km_post_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   /*
    * non-RT signals are consolidated in while pending.
    */
   if (info->si_signo < SIGRTMIN && signal_pending(vcpu, info)) {
      return;
   }
   // See if this signal can wake a thread in sigsuspend().
   if (km_wakeup_suspended_thread(info) > 0) {
      return;
   }
   if (vcpu == 0) {
      km_infox(KM_TRACE_VCPU, "enqueuing signal %d to VM", info->si_signo);
      enqueue_signal(&machine.sigpending, info);
      return;
   }
   km_infox(KM_TRACE_VCPU, "enqueuing signal %d to vcpu %d", info->si_signo, vcpu->vcpu_id);
   enqueue_signal(&vcpu->sigpending, info);
   if (km_sigismember(&vcpu->sigmask, info->si_signo) == 0) {
      km_pkill(vcpu, KM_SIGVCPUSTOP);
   }
}

/*
 * Signal handler caller stack frame. This is what RSP points at when a guest signal
 * handler is started.
 */
typedef struct km_signal_frame {
   uint64_t return_addr;   // return address for guest handler. See runtime/x86_sigaction.s
   ucontext_t ucontext;    // Passed to guest signal handler
   siginfo_t info;         // Passed to guest signal handler
   uint64_t rflags;        // saved rflags
   /*
    * Followed by monitor dependent state.
    * For KVM this depends on the value of KVM_CAP_XSAVE.
    *   struct kvm_xsave if XSAVE is enabled
    *   struct kvm_fpu otherwise
    * For KKM this is km_signal_kkm_frame_t.
    */
} km_signal_frame_t;

#define RED_ZONE (128)

static inline void save_signal_context(km_vcpu_t* vcpu, km_signal_frame_t* frame)
{
   ucontext_t* uc = &frame->ucontext;
   uc->uc_mcontext.gregs[REG_RAX] = vcpu->regs.rax;
   uc->uc_mcontext.gregs[REG_RBX] = vcpu->regs.rbx;
   uc->uc_mcontext.gregs[REG_RCX] = vcpu->regs.rcx;
   uc->uc_mcontext.gregs[REG_RDX] = vcpu->regs.rdx;
   uc->uc_mcontext.gregs[REG_RSI] = vcpu->regs.rsi;
   uc->uc_mcontext.gregs[REG_RDI] = vcpu->regs.rdi;
   uc->uc_mcontext.gregs[REG_RBP] = vcpu->regs.rbp;
   uc->uc_mcontext.gregs[REG_RSP] = vcpu->regs.rsp;
   uc->uc_mcontext.gregs[REG_R8] = vcpu->regs.r8;
   uc->uc_mcontext.gregs[REG_R9] = vcpu->regs.r9;
   uc->uc_mcontext.gregs[REG_R10] = vcpu->regs.r10;
   uc->uc_mcontext.gregs[REG_R11] = vcpu->regs.r11;
   uc->uc_mcontext.gregs[REG_R12] = vcpu->regs.r12;
   uc->uc_mcontext.gregs[REG_R13] = vcpu->regs.r13;
   uc->uc_mcontext.gregs[REG_R14] = vcpu->regs.r14;
   uc->uc_mcontext.gregs[REG_R15] = vcpu->regs.r15;
   uc->uc_mcontext.gregs[REG_RIP] = vcpu->regs.rip;

   frame->rflags = vcpu->regs.rflags;
   memcpy(&frame->ucontext.uc_sigmask, &vcpu->sigmask, sizeof(vcpu->sigmask));

   void* fp_frame = (frame + 1);
   if (km_vmdriver_save_fpstate(vcpu, fp_frame, km_vmdriver_fp_format(vcpu)) < 0) {
      // TODO: SIGFPE?
      km_warnx("Error saving FP state for signal");
   }
}

static inline void restore_signal_context(km_vcpu_t* vcpu, km_signal_frame_t* frame)
{
   ucontext_t* uc = &frame->ucontext;
   vcpu->regs.rax = uc->uc_mcontext.gregs[REG_RAX];
   vcpu->regs.rbx = uc->uc_mcontext.gregs[REG_RBX];
   vcpu->regs.rcx = uc->uc_mcontext.gregs[REG_RCX];
   vcpu->regs.rdx = uc->uc_mcontext.gregs[REG_RDX];
   vcpu->regs.rsi = uc->uc_mcontext.gregs[REG_RSI];
   vcpu->regs.rdi = uc->uc_mcontext.gregs[REG_RDI];
   vcpu->regs.rbp = uc->uc_mcontext.gregs[REG_RBP];
   vcpu->regs.rsp = uc->uc_mcontext.gregs[REG_RSP];
   vcpu->regs.r8 = uc->uc_mcontext.gregs[REG_R8];
   vcpu->regs.r9 = uc->uc_mcontext.gregs[REG_R9];
   vcpu->regs.r10 = uc->uc_mcontext.gregs[REG_R10];
   vcpu->regs.r11 = uc->uc_mcontext.gregs[REG_R11];
   vcpu->regs.r12 = uc->uc_mcontext.gregs[REG_R12];
   vcpu->regs.r13 = uc->uc_mcontext.gregs[REG_R13];
   vcpu->regs.r14 = uc->uc_mcontext.gregs[REG_R14];
   vcpu->regs.r15 = uc->uc_mcontext.gregs[REG_R15];
   vcpu->regs.rip = uc->uc_mcontext.gregs[REG_RIP];

   vcpu->regs.rflags = frame->rflags;
   memcpy(&vcpu->sigmask, &frame->ucontext.uc_sigmask, sizeof(vcpu->sigmask));

   void* fp_frame = (frame + 1);
   if (km_vmdriver_restore_fpstate(vcpu, fp_frame, km_vmdriver_fp_format(vcpu)) < 0) {
      // TODO: SIGFPE?
      km_warnx("Error restoring FP state on signal return");
   }
}

/*
 * Do the dirty-work to get a signal handler called in the guest.
 * We set everything up so the next time the vcpu runs it will be running the
 * signal handler.
 */
static inline void do_guest_handler(km_vcpu_t* vcpu, siginfo_t* info, km_sigaction_t* act)
{
   km_infox(KM_TRACE_SIGNALS, "Enter: signo=%d", info->si_signo);
   vcpu->regs_valid = 0;
   vcpu->sregs_valid = 0;
   km_vcpu_sync_rip(vcpu);   // sync RIP with KVM
   km_read_registers(vcpu);

   km_gva_t sframe_gva;
   // Check if sigaltstack is set, requested, and not in use yet, and use it then
   if ((act->sa_flags & SA_ONSTACK) == SA_ONSTACK && vcpu->sigaltstack.ss_size != 0 &&
       km_on_altstack(vcpu, vcpu->regs.rsp) == 0) {
      sframe_gva = (km_gva_t)vcpu->sigaltstack.ss_sp + vcpu->sigaltstack.ss_size;
   } else {
      sframe_gva = vcpu->regs.rsp - RED_ZONE;
   }
   // Calculate size of saved floating point state (depends on VM driver)
   size_t fstate_size = km_vmdriver_fpstate_size();
   // Align stack to 16 bytes per X86_64 ABI.
   sframe_gva = rounddown(sframe_gva - (sizeof(km_signal_frame_t) + fstate_size), 16) - 8;
   km_signal_frame_t* frame = km_gva_to_kma_nocheck(sframe_gva);

   frame->info = *info;
   save_signal_context(vcpu, frame);
   if ((act->sa_flags & SA_RESTORER) != 0) {
      frame->return_addr = act->restorer;
   } else {
      frame->return_addr = km_guest_kma_to_gva(&__km_sigreturn);
   }
   if ((act->sa_flags & SA_SIGINFO) != 0) {
      vcpu->sigmask |= act->sa_mask;
   }
   // Defer this signal.
   km_sigaddset(&vcpu->sigmask, info->si_signo);

   vcpu->regs.rsp = sframe_gva;
   vcpu->regs.rip = act->handler;
   vcpu->regs.rdi = info->si_signo;
   vcpu->regs.rsi = sframe_gva + offsetof(km_signal_frame_t, info);
   vcpu->regs.rdx = sframe_gva + offsetof(km_signal_frame_t, ucontext);

   km_write_registers(vcpu);

   km_infox(KM_TRACE_SIGNALS, "Call: RIP 0x%0llx RSP 0x%0llx", vcpu->regs.rip, vcpu->regs.rsp);
}

/*
 * Deliver signal to guest. What delivery looks like depends on the signal disposition.
 * Ignored signals are ignored.
 * Default handler signals typically terminate, possibly with a core,
 * Handled signals result in the guest being setup to run the signal handler
 * on the next VM_RUN call.
 */
void km_deliver_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   km_sigaction_t* act = &machine.sigactions[km_sigindex(info->si_signo)];
   if (act->handler == (km_gva_t)SIG_IGN) {
      return;
   }
   if (act->handler == (km_gva_t)SIG_DFL) {
      if (km_sigismember(&def_ign_signals, info->si_signo) != 0) {
         return;
      }

      // Signals that get here terminate the process. The only question is: core or no core?
      int core_dumped = 0;
      assert(info->si_signo != SIGCHLD);   // KM does not support
      km_vcpu_pause_all(vcpu, GUEST_ONLY);
      if ((km_sigismember(&perror_signals, info->si_signo) != 0) || (info->si_signo == SIGQUIT)) {
         extern int debug_dump_on_err;
         km_dump_core(km_get_coredump_path(), vcpu, NULL, NULL, "Signal Delivery");
         if (debug_dump_on_err) {
            abort();
         }
         core_dumped = 1;
      }
      km_errx(info->si_signo,
              "guest: Terminated by signal: %s %s",
              strsignal(info->si_signo),
              (core_dumped) ? "(core dumped)" : "");
   }

   assert(act->handler != (km_gva_t)SIG_IGN);
   do_guest_handler(vcpu, info, act);
}

void km_rt_sigreturn(km_vcpu_t* vcpu)
{
   km_read_registers(vcpu);
   /*
    * The guest's signal restorer makes a syscall which goes into the KM syscall handler.
    * We add the size of the km_hc_args_t that out syscall handler adds to the stack.
    * We substract the size of an address to account for the fact that the return address
    * (in the frame) was consumed by the return to the signal restorer.
    *
    * Note: this needs to be kept in sync with __km_syscall_handler.
    */
   km_signal_frame_t* frame =
       km_gva_to_kma_nocheck(vcpu->regs.rsp + sizeof(km_hc_args_t) - sizeof(km_gva_t));
   // check if we use sigaltstack is used, and we are leaving it now
   if (km_on_altstack(vcpu, vcpu->regs.rsp) == 1 &&
       km_on_altstack(vcpu, frame->ucontext.uc_mcontext.gregs[REG_RSP]) == 0) {
      vcpu->sigaltstack.ss_flags = 0;
   }
   restore_signal_context(vcpu, frame);
   km_write_registers(vcpu);
   km_info(KM_TRACE_SIGNALS, "Return: RIP 0x%0llx RSP 0x%0llx", vcpu->regs.rip, vcpu->regs.rsp);
}

uint64_t
km_rt_sigprocmask(km_vcpu_t* vcpu, int how, km_sigset_t* set, km_sigset_t* oldset, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
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
               if (km_sigismember(set, signo) != 0) {
                  km_sigaddset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_UNBLOCK:
            for (int signo = 1; signo < _NSIG; signo++) {
               if (km_sigismember(set, signo) != 0) {
                  km_sigdelset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_SETMASK:
            vcpu->sigmask = *set;
            break;
         default:
            return -EINVAL;
      }
   }
   return 0;
}

/*
 * Set a guest signal handling function for the passed signal.
 */
uint64_t
km_rt_sigaction(km_vcpu_t* vcpu, int signo, km_sigaction_t* act, km_sigaction_t* oldact, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   if (signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }
   if (km_sigismember(&no_catch_signals, signo) != 0) {
      return -EINVAL;
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

uint64_t km_sigaltstack(km_vcpu_t* vcpu, km_stack_t* new, km_stack_t* old)
{
   if (old != NULL) {
      old->ss_sp = vcpu->sigaltstack.ss_sp;
      old->ss_size = vcpu->sigaltstack.ss_size;
      old->ss_flags = old->ss_size == 0 ? SS_DISABLE : vcpu->sigaltstack.ss_flags;
   }
   if (new != NULL) {
      if (new->ss_flags != SS_DISABLE && new->ss_flags != SS_ONSTACK && new->ss_flags != 0) {
         return -EINVAL;
      }
      km_read_registers(vcpu);
      if (km_on_altstack(vcpu, vcpu->regs.rsp)) {   // in use
         return -EPERM;
      }
      if (new->ss_flags == SS_DISABLE) {
         vcpu->sigaltstack.ss_size = 0;
         vcpu->sigaltstack.ss_sp = NULL;
      } else {
         if (new->ss_size < MINSIGSTKSZ) {
            return -ENOMEM;
         }
         if (km_gva_to_kma((km_gva_t) new->ss_sp) == NULL) {
            return -EFAULT;
         }
         vcpu->sigaltstack.ss_sp = (void*)new->ss_sp;
         vcpu->sigaltstack.ss_size = new->ss_size;
         vcpu->sigaltstack.ss_flags = 0;
         km_infox(KM_TRACE_HC, "new sigaltstack 0x%lx 0x%lx", (km_gva_t) new->ss_sp, new->ss_size);
      }
   }
   return 0;
}

uint64_t km_kill(km_vcpu_t* vcpu, pid_t pid, int signo)
{
   if (signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }
   pid_t linux_pid = km_pid_xlate_kpid(pid);
   if (linux_pid != pid) {   // signal to another instance of km
      if (kill(linux_pid, signo) < 0) {
         return -errno;
      }
   } else {
      if (pid != 0 && pid != machine.pid) {
         return -ESRCH;
      }

      // Process-wide signal.
      siginfo_t info = {.si_signo = signo, .si_code = SI_USER};
      km_post_signal(NULL, &info);
   }
   return 0;
}

uint64_t km_tkill(km_vcpu_t* vcpu, pid_t tid, int signo)
{
   km_vcpu_t* target_vcpu = km_vcpu_fetch_by_tid(tid);

   if (target_vcpu == NULL || signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }

   // Thread-targeted signal.
   siginfo_t info = {.si_signo = signo, .si_pid = machine.pid, .si_code = SI_TKILL};
   km_post_signal(target_vcpu, &info);
   return 0;
}

/*
 * Return the set of pending signals for both the passed vcpu and for the
 * virtual machine in set.
 */
uint64_t km_rt_sigpending(km_vcpu_t* vcpu, km_sigset_t* set, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   get_pending_signals(vcpu, set);
   return 0;
}


uint64_t km_rt_sigsuspend(km_vcpu_t* vcpu, km_sigset_t* mask, size_t masksize)
{
   int signo;
   if (masksize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   km_infox(KM_TRACE_VCPU,
            "start waiting for signal, new mask 0x%lx, prev mask 0x%lx",
            mask[0],
            vcpu->sigmask);

   km_signal_lock();

   vcpu->in_sigsuspend = 1;
   vcpu->saved_sigmask = vcpu->sigmask;
   vcpu->sigmask = *mask;
   TAILQ_INSERT_TAIL(&km_signal_wait_queue, vcpu, signal_link);
   while ((signo = km_signal_ready_nolock(vcpu)) == 0) {
      km_cond_wait(&vcpu->signal_wait_cv, &machine.signal_mutex);
      km_infox(KM_TRACE_VCPU, "waking up in sigsuspend");
   }
   // We restore sigmask after setting up the signal handler call in vcpu_run
   km_signal_unlock();

   km_infox(KM_TRACE_VCPU, "signal %d arrival unsuspends thread", signo);

   return -EINTR;
}

/*
 * Dump in elf format
 */
size_t km_sig_core_notes_length()
{
   size_t ret = 0;
   for (int i = 1; i <= _NSIG; i++) {
      km_sigaction_t* sa = &machine.sigactions[km_sigindex(i)];
      if (sa->handler != (uintptr_t)SIG_DFL) {
         ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_sighand_t);
      }
   }
   return ret;
}

size_t km_sig_core_notes_write(char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;
   for (int i = 1; i <= _NSIG; i++) {
      km_sigaction_t* sa = &machine.sigactions[km_sigindex(i)];
      if (sa->handler != (uintptr_t)SIG_DFL) {
         cur += km_add_note_header(cur, remain, KM_NT_NAME, NT_KM_SIGHAND, sizeof(km_nt_sighand_t));
         km_nt_sighand_t* nt_sa = (km_nt_sighand_t*)cur;
         nt_sa->size = sizeof(km_nt_sighand_t);
         nt_sa->signo = i;
         nt_sa->handler = sa->handler;
         nt_sa->mask = sa->sa_mask;
         nt_sa->flags = sa->sa_flags;

         cur += sizeof(km_nt_sighand_t);
      }
   }
   return cur - buf;
}

int km_sig_snapshot_recover(char* buf, size_t length)
{
   km_nt_sighand_t* nt_sa = (km_nt_sighand_t*)buf;
   if (nt_sa->size != sizeof(km_nt_sighand_t)) {
      km_warnx("km_nt_sighand_t size mismatch - old snapshot?");
      return -1;
   }
   km_infox(KM_TRACE_SNAPSHOT, "signal %d handler=0x%lx", nt_sa->signo, nt_sa->handler);
   km_sigaction_t sa = {
       .handler = nt_sa->handler,
       .sa_flags = nt_sa->flags,
       .sa_mask = nt_sa->mask,
   };
   machine.sigactions[km_sigindex(nt_sa->signo)] = sa;
   return 0;
}
