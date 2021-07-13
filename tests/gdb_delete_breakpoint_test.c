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

/*
 * Helper for gdb delete breakpoint test where one thread hits a breakpoint in a loop
 * and the main thread disables and enables the other thread's breakpoint.
 * The goal is to have the breakpoint disabled while the thread hits that
 * breakpoint.  This resuls in a situation where the breakpoint that disables
 * and the breakpoint that is disabled happen in that order and close enough
 * in time that we have a pending gdb event for a breakpoint that no longer
 * exists.  gdb should handle that.
 * This test program runs with the gdb script cmd_for_delete_breakpoint_test.gdb
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

struct timespec _100ms = {0, 100000000};

int enable_disable_ready = 0;
int breakpointer_ready = 0;
pthread_barrier_t barrier;
pthread_barrier_t end_barrier;
int stop_now = 0;

/*
 * A breakpoint target so we don't need to place a breakpoint on a source
 * file line number.
 */
static time_t __attribute__((noinline)) disable_breakpoint(void)
{
   return time(NULL);
}
static time_t __attribute__((noinline)) enable_breakpoint(void)
{
   return time(NULL);
}
static time_t __attribute__((noinline)) hit_breakpoint(void)
{
   return time(NULL);
}

/*
 * The bats test driver will start this program under gdb and then
 * the gdb client will be driven from cmd_for_delete_breakpoint_test.gdb
 * This program creates one thread that just loops hitting a breakpoint.
 * The main thread hits 2 breakpoints repeatedly.  One breakpoint disables
 * the other thread's breakpoint and the other breakpoint enables the
 * other thread's breakpoint.
 * The goal is to have the main thread's breakpoint disable happen just before
 * the second thread's breakpoint is hit.  This condition leads to a deleted
 * breakpoint happening just after a breakpoint is hit.
 * We use a pthread barrier to get the 2 threads approximately synchronized.
 * And then we use flags for each thread to signal to the other that they
 * are about to hit their respective breakpoints.  Each thread waits in a
 * spin loop for the other thread to be ready.  All of this just helps
 * increase the odds of the desired race happening.
 * We can grep for a km_trace() output from km noting that a triggered
 * breakpoint was deleted because it was pending and the breakpoint that
 * caused it was deleted.
 */

#define RUNTIME 1   // seconds

void* hit_breakpoint_thread(void* arg)
{
   int i = 0;
   int rc;

   printf("%s starting\n", __FUNCTION__);

   while (true) {
      rc = pthread_barrier_wait(&barrier);
      assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
      if (stop_now != 0) {
         break;
      }
      breakpointer_ready = 1;
      while (enable_disable_ready == 0)
         ;
      for (int j = 0; j < 100; j++)
         ;
      (void)hit_breakpoint();
      i++;

      // Wait for both threads to finish their part of the test before doing another iteration
      rc = pthread_barrier_wait(&end_barrier);
      assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
   }

   printf("%s ending, %d iterations\n", __FUNCTION__, i);

   return NULL;
}

int main()
{
   int rc;
   pthread_t hitbp_thread;
   time_t starttime;
   time_t t1;
   time_t t2;
   int i = 0;

   rc = pthread_barrier_init(&barrier, NULL, 2);
   assert(rc == 0);
   rc = pthread_barrier_init(&end_barrier, NULL, 2);
   assert(rc == 0);

   // Start the breakpoint hitting thread
   rc = pthread_create(&hitbp_thread, NULL, hit_breakpoint_thread, NULL);
   assert(rc == 0);

   // Enable and disable the breakpoint in the hit_breakpoint_thread()
   starttime = time(NULL);
   while (true) {
      enable_disable_ready = 0;
      breakpointer_ready = 0;
      pthread_barrier_wait(&barrier);
      if (stop_now != 0) {
         break;
      }
      enable_disable_ready = 1;
      while (breakpointer_ready == 0)
         ;
      // for (int j = 0; j < 100; j++);
      t1 = disable_breakpoint();
      t2 = enable_breakpoint();
      if (((t1 + t2) / 2 - starttime) > RUNTIME) {
         struct timespec st = _100ms;
         struct timespec rem;
         // pause to let the other thread block in barrier wait
         while (nanosleep(&st, &rem) != 0 && errno == EINTR) {
            st = rem;
         }
         stop_now = 1;
      }
      i++;

      // Wait for both threads to finish their part of the test before doing another iteration
      rc = pthread_barrier_wait(&end_barrier);
      assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
   }

   printf("%s ending, %d iterations\n", __FUNCTION__, i);

   return 0;
}
