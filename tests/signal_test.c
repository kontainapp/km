/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Simple test for signal system call.
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "greatest/greatest.h"
#include "syscall.h"

int signal_seen = 0;

void signal_handler(int signal)
{
   signal_seen++;
}

/*
 * Ensure that a simple signal is received.
 */
TEST test_simple_signal()
{
   signal_seen = 0;
   signal(SIGTERM, signal_handler);
   kill(0, SIGTERM);
   ASSERT_EQ(1, signal_seen);
   signal(SIGTERM, SIG_DFL);
   PASS();
}

/*
 * multiple signals masked out and re-enabled.
 */
TEST test_sigpending()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGUSR1, signal_handler);
   signal(SIGUSR2, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGUSR1);
   sigaddset(&ss, SIGUSR2);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGUSR1);
   kill(0, SIGUSR2);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NOT_EQ(0, sigismember(&pending, SIGUSR1));
   ASSERT_NOT_EQ(0, sigismember(&pending, SIGUSR2));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(2, signal_seen);
   signal(SIGUSR1, SIG_DFL);
   signal(SIGUSR2, SIG_DFL);

   // test bad sigsetsize
   ASSERT_EQ(-1, syscall(SYS_rt_sigpending, pending, 4));
   ASSERT_EQ(EINVAL, errno);
   PASS();
}

/*
 * RT signals are queued.
 */
TEST test_sigqueued()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGRTMIN, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGRTMIN);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGRTMIN);
   kill(0, SIGRTMIN);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NOT_EQ(0, sigismember(&pending, SIGRTMIN));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(2, signal_seen);
   signal(SIGRTMIN, SIG_DFL);
   PASS();
}

/*
 * 'old' signals (< SIGRTMIN) are consolodated, not queued.
 */
TEST test_sigconsolodated()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGUSR1, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGUSR1);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGUSR1);
   kill(0, SIGUSR1);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NOT_EQ(0, sigismember(&pending, SIGUSR1));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(1, signal_seen);
   signal(SIGUSR1, SIG_DFL);
   PASS();
}

void first_signal_handler(int signo)
{
   kill(0, SIGUSR2);
}

TEST test_nested_signal()
{
   signal_seen = 0;
   signal(SIGUSR1, first_signal_handler);
   signal(SIGUSR2, signal_handler);

   ASSERT_EQ(0, signal_seen);
   kill(0, SIGUSR1);
   ASSERT_EQ(1, signal_seen);

   signal(SIGUSR1, SIG_DFL);
   signal(SIGUSR2, SIG_DFL);
   PASS();
}

TEST test_sigprocmask()
{
   sigset_t set;
   sigset_t oldset;

   sigemptyset(&set);
   sigaddset(&set, SIGUSR2);

   // Test success paths
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &set, &oldset));
   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, &oldset, NULL));
   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, NULL, &oldset));

   // test bogus 'how' to sigprocmask
   sigemptyset(&set);
   sigaddset(&set, SIGUSR2);
   ASSERT_EQ(-1, sigprocmask(57, &set, &oldset));
   ASSERT_EQ(EINVAL, errno);

   // test bad sigsetsize to sigprocmask.
   ASSERT_EQ(-1, syscall(SYS_rt_sigprocmask, SIG_BLOCK, &set, &oldset, 4));
   ASSERT_EQ(EINVAL, errno);

   PASS();
}

TEST test_sigaction()
{
   struct sigaction act;
   struct sigaction oldact;

   // get old action
   act.sa_handler = SIG_IGN;
   ASSERT_EQ(0, sigaction(SIGUSR1, NULL, &oldact));
   // Set new action
   ASSERT_EQ(0, sigaction(SIGUSR1, &act, &oldact));
   // restore old action
   ASSERT_EQ(0, sigaction(SIGUSR1, &oldact, NULL));

   // bad signal values.
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, 0, &oldact, NULL, 8));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, SIGRTMAX + 1, &oldact, NULL, 8));
   ASSERT_EQ(EINVAL, errno);

   // bad sigsetsize
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, 0, &oldact, NULL, 4));
   ASSERT_EQ(EINVAL, errno);

   PASS();
}

TEST test_kill()
{
   // Another process
   ASSERT_EQ(-1, kill(1, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);

   // test bad signal numbers
   ASSERT_EQ(-1, kill(0, 0));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, kill(0, SIGRTMAX + 1));
   ASSERT_EQ(EINVAL, errno);
   PASS();
}

TEST test_tkill()
{
   // too small for KM vcpu
   ASSERT_EQ(-1, syscall(SYS_tkill, -1, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);
   // too large for KM vcpu
   ASSERT_EQ(-1, syscall(SYS_tkill, 1024, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);
   // unused KM vcpu
   ASSERT_EQ(-1, syscall(SYS_tkill, 1, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);

   // bad signal numbers
   ASSERT_EQ(-1, syscall(SYS_tkill, 0, 0));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, syscall(SYS_tkill, 0, SIGRTMAX + 1));
   ASSERT_EQ(EINVAL, errno);
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_simple_signal);
   RUN_TEST(test_sigpending);
   RUN_TEST(test_sigqueued);
   RUN_TEST(test_sigconsolodated);
   RUN_TEST(test_nested_signal);
   RUN_TEST(test_sigprocmask);
   RUN_TEST(test_sigaction);
   RUN_TEST(test_kill);
   RUN_TEST(test_tkill);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
