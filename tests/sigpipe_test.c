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
 * Simple test for SIGPIPE and EPIPE
 */

#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "greatest/greatest.h"

int sigpipe_received = 0;

void sigpipe_handler(int sig)
{
   if (sig == SIGPIPE) {
      sigpipe_received++;
   }
}

TEST test_sigpipe()
{
   int fd[2];

   ASSERT_NOT_EQ(SIG_ERR, signal(SIGPIPE, sigpipe_handler));
   ASSERT_EQ_FMT(0, pipe(fd), "%d");
   close(fd[0]); // close the read end
   ASSERT_EQ_FMT(-1l, write(fd[1], "some message", strlen("some_message")), "%ld");
   ASSERT_EQ_FMT(EPIPE, errno, "%d");
   ASSERT_NOT_EQ_FMT(0, sigpipe_received, "%d");
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(test_sigpipe);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
