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
   signal_seen = 1;
}

TEST test_simple_signal()

{
   signal_seen = 0;
   signal(SIGTERM, signal_handler);
   kill(0, SIGTERM);
   ASSERT_EQ(1, signal_seen);
   signal(SIGTERM, SIG_DFL);
   PASS();
}

TEST test_sigpending()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGUSR1, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGUSR1);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGUSR1);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NOT_EQ(0, sigismember(&pending, SIGUSR1));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(1, signal_seen);
   signal(SIGUSR1, SIG_DFL);
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

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
