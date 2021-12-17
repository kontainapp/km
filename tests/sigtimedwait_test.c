/*
 * Copyright 2021 Kontain Inc
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

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "greatest/greatest.h"
#include "syscall.h"

/*
 * Test basic sigtimedwait() functionality.
 */

// Convert a timespec to nanosecond value
static inline uint64_t ts2ns(struct timespec* ts)
{
   return (ts->tv_sec * 1000000000ul) + ts->tv_nsec;
}

// Return the difference between 2 timespecs in nanoseconds
uint64_t timespec_delta(struct timespec* this, struct timespec* minusthis)
{
   return ts2ns(this) - ts2ns(minusthis);
}

/*
 * A thread that pauses briefly and then sends a SIGUSR1 signal that the main
 * thread should be waiting for.
 */
void* signal_sender(void* arg)
{
   int rc;

   // pause
   struct timespec sleeptime = {1, 0};
   rc = nanosleep(&sleeptime, NULL);
   if (rc != 0) {
      fprintf(stderr, "%s: nanosleep failed, %s\n", __FUNCTION__, strerror(errno));
   }
   fprintf(stdout, "Sending SIGUSR1 to main thread\n");

   // send signal to the main thread
   pid_t threadid = (pid_t)(uint64_t)arg;
   rc = syscall(SYS_tkill, threadid, SIGUSR1);
   if (rc != 0) {
      fprintf(stderr, "%s: SYS_tkill failed, %s\n", __FUNCTION__, strerror(errno));
   }

   return NULL;
}

TEST sigtimedwait_test(void)
{
   struct timespec start, end;
   struct timespec thislong;
   int signo;
   sigset_t set;
   sigset_t oldset;
   siginfo_t info;
   int rc;

   // Call sigtimedwait() with an invalid wait time.  Should fail with EINVAL.
   fprintf(stdout, "Testing sigtimedwait() with invalid wait time\n");
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   thislong.tv_sec = 0;
   thislong.tv_nsec = 10L * 1000 * 1000 * 1000;   // 10 seconds in nanoseconds
   signo = sigtimedwait(&set, &info, &thislong);
   ASSERT_EQ(-1, signo);
   ASSERT_EQ(EINVAL, errno);

   // wait for a signal that never arrives with a timeout of 0
   fprintf(stdout, "Testing sigtimedwait() with wait time of 0\n");
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   thislong.tv_sec = 0;
   thislong.tv_nsec = 0;
   clock_gettime(CLOCK_MONOTONIC, &start);
   signo = sigtimedwait(&set, &info, &thislong);
   ASSERT_EQ(-1, signo);
   ASSERT_EQ(EAGAIN, errno);
   clock_gettime(CLOCK_MONOTONIC, &end);
   ASSERTm("sigtimedwait() with no wait took too long", timespec_delta(&end, &start) < 100000000ul);

   /*
    * wait for a signal that never arrives with a short timeout, verify that timeout is approximately
    * correct.  I will guess that this test may fail on azure during high load periods.
    */
   fprintf(stdout, "Testing sigtimedwait() with a short wait time\n");
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   thislong.tv_sec = 0;
   thislong.tv_nsec = 250000000;   // .25 seconds
   clock_gettime(CLOCK_MONOTONIC, &start);
   signo = sigtimedwait(&set, &info, &thislong);
   ASSERT_EQ(-1, signo);
   ASSERT_EQ(EAGAIN, errno);
   clock_gettime(CLOCK_MONOTONIC, &end);
   uint64_t deltat = timespec_delta(&end, &start);
   fprintf(stderr,
           "Should wait for %ld nanoseconds, actually waited %ld nanoseconds\n",
           thislong.tv_nsec,
           deltat);
#define TIME_ERROR_MARGIN 50000000ul
   ASSERTm("sigtimedwait() didn't wait long enough", deltat > thislong.tv_nsec - TIME_ERROR_MARGIN);
   ASSERTm("sigtimedwait() waited too long", deltat < thislong.tv_nsec + TIME_ERROR_MARGIN);

   // Post a blocked signal and then wait for it
   fprintf(stdout, "Testing sigtimedwait() waiting for a blocked signal\n");
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   rc = sigprocmask(SIG_BLOCK, &set, &oldset);
   ASSERT_EQ(0, rc);
   rc = kill(getpid(), SIGUSR1);
   ASSERT_EQ(0, rc);
   memset(&info, 0, sizeof(info));
   thislong.tv_sec = 0;
   thislong.tv_nsec = 0;
   signo = sigtimedwait(&set, &info, &thislong);
   fprintf(stderr, "signo %d, info.si_signo %d, errno %d, %s\n", signo, info.si_signo, errno, strerror(errno));
   ASSERT_EQ(SIGUSR1, signo);
   ASSERT_EQ(SIGUSR1, info.si_signo);
   rc = sigprocmask(SIG_SETMASK, &oldset, NULL);
   ASSERT_EQ(0, rc);

   /*
    * Have this thread block indefinitely waiting for SIGUSR1 and spawn a thread that will
    * send SIGUSR1.
    * This test may fail on heavily loaded systems if this thread (main) is stalled
    * and the signal sender thread sends SIGUSR1 before this thread has blocked waiting for
    * it.
    */
   fprintf(stdout, "Testing sigtimedwait() waiting indefinitely for a signal\n");
   memset(&info, 0, sizeof(info));
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   rc = sigprocmask(SIG_BLOCK, &set, &oldset);
   ASSERT_EQ(0, rc);

   pthread_t thread;
   void* arg = (void*)(uint64_t)syscall(SYS_gettid);
   rc = pthread_create(&thread, NULL, signal_sender, arg);
   ASSERT_EQ(0, rc);

   // Wait for a signal from the thread
   fprintf(stdout, "About to call sigtimedwait()\n");
   signo = sigtimedwait(&set, &info, NULL);
   ASSERT_EQ(SIGUSR1, signo);
   ASSERT_EQ(SIGUSR1, info.si_signo);
   // wait for signal sender thread to terminate
   void* voidrv;
   rc = pthread_join(thread, &voidrv);
   ASSERT_EQ(0, rc);
   rc = sigprocmask(SIG_SETMASK, &oldset, NULL);
   ASSERT_EQ(0, rc);

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(sigtimedwait_test);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
