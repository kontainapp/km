/*
 * Copyright 2021 Kontain Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Simple test for SIGPIPE and EPIPE
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

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

   ASSERT_NEQ(SIG_ERR, signal(SIGPIPE, sigpipe_handler));
   ASSERT_EQ_FMT(0, pipe(fd), "%d");
   close(fd[0]);   // close the read end
   ASSERT_EQ_FMT(-1l, write(fd[1], "some message", strlen("some_message")), "%ld");
   ASSERT_EQ_FMT(EPIPE, errno, "%d");
   ASSERT_NEQ_FMT(0, sigpipe_received, "%d");
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
