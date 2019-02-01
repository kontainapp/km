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
#include <sys/signalfd.h>
#include <linux/kvm.h>

#include "km.h"
#include "km_signal.h"

/*
 * Clear the requested signal for a VCPU, at the same time blocking it
 * for current thread so it can get through while in KVM.
 * TBD: we need to fetch process mask, clear the signal and set it.
 * For now clearing all.
 */
void km_vcpu_unblock_signal(km_vcpu_t* vcpu, int signum)
{
   sigset_t signal_set;
   sigemptyset(&signal_set);
   struct kvm_signal_mask* sigmask;

   sigmask = malloc(sizeof(struct kvm_signal_mask) + sizeof(sigset_t));
   if (sigmask == NULL) {
      err(1, "Can't allocate memory for setting KVM signal mask");
   }

   // KVM will expect kernel_sigset_t with long-sized (8 in i86_64) signal_set
   sigmask->len = 8;
   memcpy(sigmask->sigset, &signal_set, sizeof(sigset_t));
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_SIGNAL_MASK, sigmask) < 0) {
      free(sigmask);
      err(1, "Failed to unblock signal %d for KVM", signum);
   }

   sigaddset(&signal_set, signum);
   if (pthread_sigmask(SIG_BLOCK, &signal_set, NULL) != 0) {
      free(sigmask);
      err(1, "Failed to block signal %d on thread 0x%lx", signum, pthread_self());
   }
   free(sigmask);
}

/*
 * Construct and return signalfd for a signal, so we can
 * poll() on it. Also blocks the signal on the current thread
 * so it will be "pending" and thus delivered via socketfd.
 */
int km_get_signalfd(int signum)
{
   sigset_t signal_set;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
   return signalfd(-1, &signal_set, SFD_NONBLOCK | SFD_CLOEXEC);
}

/*
 * Resets signal if pending. Basically, just sets sigaction to 'ignore'
 * which will reset pending, if any, and then restore sigaction back
 */
void km_reset_pending_signal(int signum)
{
   struct sigaction prev;   // previous info - to recover after reset
   struct sigaction sa_ignore = {.sa_handler = SIG_IGN, .sa_flags = 0};

   sigemptyset(&sa_ignore.sa_mask);
   if (sigaction(signum, &sa_ignore, &prev) < 0) {
      err(1, "Couldn't ignore signal %d", signum);
   }
   if (sigaction(signum, &prev, NULL) < 0) {
      err(1, "Couldn't restore default behavior for signal %d", signum);
   }
   km_infox("Cancelled (potentially) pending signal %d", signum);
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
   int received_signal;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
   sigwait(&signal_set, &received_signal);
}
