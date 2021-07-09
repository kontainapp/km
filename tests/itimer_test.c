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
 * Simple test to exercise setitimer() and getitimer().
 */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

int itimer_fire_count;

void signal_itimer_real(int signo, siginfo_t* sinfo, void* ucontext)
{
   char message[] = "In SIGALRM signal handler\n";
   write(1, message, strlen(message));
   itimer_fire_count++;
}

int main(int argc, char* argv[])
{
   // Setup signal handler.
   struct sigaction sa;
   sa.sa_sigaction = signal_itimer_real;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGALRM, &sa, NULL) < 0) {
      fprintf(stderr, "sigaction( SIGALRM ) failed, %s\n", strerror(errno));
      return 1;
   }

   // Setup an ITIMER_REAL interval timer
   struct itimerval new = {
       { 0, 50000 },		// interval (50ms)
       { 0, 20000 }             // time to next expiration
   };
   if (setitimer(ITIMER_REAL, &new, NULL) < 0) {
      fprintf(stderr, "setitimer( ITIMER_REAL ) failed, %s\n", strerror(errno));
      return 1;
   }

   // Read back the interval timer
   struct itimerval current;
   if (getitimer(ITIMER_REAL, &current) < 0) {
      fprintf(stderr, "getitimer( ITIMER_REAL ) failed, %s\n", strerror(errno));
      return 1;
   }
   fprintf(stdout, "it_interval = { %ld.%06ld sec }, it_value = { %ld.%06ld sec }\n",
           current.it_interval.tv_sec, current.it_interval.tv_usec,
           current.it_value.tv_sec, current.it_value.tv_usec);

   /*
    * wait for the timer to fire.  Note that azure can block this process/thread for
    * a long time.  Long enough so that the interval timer can fire before we ever
    * get to the call to sigsuspend().  So, we let the timer run as a real interval
    * timer to keep getting signals until this thread finally does run.
    */
   sigset_t blockme;
   sigemptyset(&blockme);
   sigsuspend(&blockme);

   // cancel the interval timer
   memset(&new, 0, sizeof(new));
   if (setitimer(ITIMER_REAL, &new, NULL) < 0) {
      fprintf(stderr, "canceling itimer( ITIMER_REAL ) failed, %s\n", strerror(errno));
      return 1;
   }

   fprintf(stdout, "itimer_fire_count = %d\n", itimer_fire_count);

   return 0;
}
