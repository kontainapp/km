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
 * Helper for gdb server entry race between a breakpoint and a seg fault.
 * The basic test is to have one thread hit a breakpoint and then have another
 * thread cause a seg fault.  We arrange for the breapointing thread to stall
 * in km_gdb_notify_and_wait() to give another thread a chance to get the
 * seg fault.  The sigill should take priority over the preceeding breakpoint
 * and should be reported back to the gdb client.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>


/*
 * Signal handler for SIGILL
 */
void handle_sigill(int signo)
{
   struct timespec delay = { 0, 50000 };  // 50 usec
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
 * the gdb client will be driven from cmd_for_gdbserverrace_test.gdb
 * The main thread should just keep hitting breakpoints and the
 * sigill thread will execute an illegal instruction which will
 * cause the handle_sigill() signal handler to be called until
 * the test ends.
 */

#define RUNTIME	1   // seconds

void* sigill_thread(void* arg)
{
   int rc;
   struct sigaction newsa = { .sa_handler = handle_sigill, .sa_flags = 0 }; 

   // Setup signal handler for illegal instructions
   sigemptyset(&newsa.sa_mask);
   rc = sigaction(SIGILL, &newsa, NULL);
   assert(rc == 0);

   // Generate an illegal instruction fault for the signal handler to catch.
   asm("ud2");
   // We will never reach this point

   // Terminate
   return NULL;
}


int main()
{
   int rc;
   pthread_t sfthread;
   time_t starttime;
   time_t t;

   // Start the sigill thread
   rc = pthread_create(&sfthread, NULL, sigill_thread, NULL);
   assert(rc == 0);

   starttime = time(NULL);
   while (true) {
      // Hit our breakpoint
      t = hit_breakpoint();
      if ((t - starttime) > RUNTIME) {
         /*
          * The sigill_thread() is just hitting the same illegal instruction and it
          * is easiest to stop the test by exiting.
          */
         exit(0);
      }
   }

   // All done.  The test driver can look at the gdb output to see if the sigill took precedence.

   return 0;
}
