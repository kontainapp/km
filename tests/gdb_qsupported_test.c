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
 * Helper for gdb server entry race between a breakpoint and a seg fault.
 * The basic test is to have one thread hit a breakpoint and then have another
 * thread cause a seg fault.  We arrange for the breapointing thread to stall
 * in km_gdb_notify_and_wait() to give another thread a chance to get the
 * seg fault.  The sigill should take priority over the preceeding breakpoint
 * and should be reported back to the gdb client.
 */
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Signal handler for SIGILL
 */
void handle_sigill(int signo)
{
   struct timespec delay = {0, 50000};   // 50 usec
   nanosleep(&delay, NULL);
}

/*
 * A breakpoint target so we don't need to place a breakpoint on a source
 * file line number.
 */
static time_t __attribute__((noinline)) hit_breakpoint(void)
{
   return time(NULL);
}

/*
 * The test driver will start this program under gdb and then
 * the gdb client will be driven from cmd_for_gdb_qsupported_test.gdb
 * The main thread should just joins the started thread.
 * The started thread should hit a breakpoint and then continue.
 */

void* breakpoint_thread(void* arg)
{
   hit_breakpoint();

   return NULL;
}

int main()
{
   int rc;
   pthread_t sfthread;

   // Start the breakpoint thread
   rc = pthread_create(&sfthread, NULL, breakpoint_thread, NULL);
   assert(rc == 0);

   rc = pthread_join(sfthread, NULL);
   assert(rc == 0);

   // All done.  The test driver can look at the gdb output to see if the gdb Switching task message
   // appeared.

   return 0;
}
