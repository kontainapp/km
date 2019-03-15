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

#include "km.h"
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
   int received_signal;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
   sigwait(&signal_set, &received_signal);
}
