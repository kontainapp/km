/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
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
#include <unistd.h>
#include <syscall.h>
#include <errno.h>

/*
 * When time() utilizes the time functions in the vdso page, things
 * run faster which reduces the frequency of the race we are trying
 * to produce.  So, add this delay to ensure the race does happen.
 */
struct timespec vdso_compensate = { 0, 1200 };  // 1.2us

/*
 * A breakpoint target so we don't need to place a breakpoint on a source
 * file line number.
 */
static time_t __attribute__((noinline)) hit_breakpoint1(void)
{
   int r;
   r = nanosleep(&vdso_compensate, NULL);
   assert(r == 0 || errno == EINTR);
   return time(NULL);
}
static time_t __attribute__((noinline)) hit_breakpoint2(void)
{
   int r;
   r = nanosleep(&vdso_compensate, NULL);
   assert(r == 0 || errno == EINTR);
   return time(NULL);
}
static time_t __attribute__((noinline)) hit_breakpoint_t1(void)
{
   int r;
   r = nanosleep(&vdso_compensate, NULL);
   assert(r == 0 || errno == EINTR);
   return time(NULL);
}
static time_t __attribute__((noinline)) hit_breakpoint_t2(void)
{
   int r;
   r = nanosleep(&vdso_compensate, NULL);
   assert(r == 0 || errno == EINTR);
   return time(NULL);
}

/*
 * The test driver will start this program under gdb and then
 * the gdb client will be driven from cmd_for_gdb_breakpoint_delete_test.gdb
 * This program creates 2 threads that just loop calling functions that will
 * have a breakpoint set on them.
 * Both threads will keep hitting breakpoints while the main thread hits
 * 2 breakpoints repeatedly.
 * The breakpoint handler for main thread hit_breakpoint1() will delete the breakpoint
 * for hit_breakpoint_thread2() and the breakpoint handler for hit_breakpoint2()
 * will add the breakpoint for hit_breakpoint_t2().
 * The breakpoint handler for hit_breakpoint_t1 and hit_breakpoint_t2() will continue.
 * The goal is to delete the breakpoint at hit_breakpoint_t2() while a breakpoint for that location
 * is pending but not delivered to the gdb client yet.  When the breakpoint
 * is deleted its pending breakpoint should also be deleted.
 * We can grep for a km_trace() in the output from km noting that a triggered
 * breakpoint was deleted because it was pending and the breakpoint that
 * caused it was deleted.
 */

#define RUNTIME 1   // seconds

void* hit_breakpoint_thread1(void* arg)
{
   time_t starttime;
   time_t t;
   int i = 0;

   printf("%s starting\n", __FUNCTION__);

   starttime = time(NULL);
   while (true) {
      t = hit_breakpoint_t1();
      if ((t - starttime) > RUNTIME) {
         break;
      }
      i++;
   }

   printf("%s ending, %d iterations\n", __FUNCTION__, i);

   return NULL;
}

void* hit_breakpoint_thread2(void* arg)
{
   time_t starttime;
   time_t t;
   int i = 0;

   printf("%s starting\n", __FUNCTION__);

   starttime = time(NULL);
   while (true) {
      t = hit_breakpoint_t2();
      if ((t - starttime) > RUNTIME) {
         break;
      }
      i++;
   }

   printf("%s ending, %d iterations\n", __FUNCTION__, i);

   return NULL;
}

int main()
{
   int rc;
   pthread_t sfthread1;
   pthread_t sfthread2;
   time_t starttime;
   time_t t1;
   time_t t2;
   int i = 0;

   // Start the breakpoint hitting threads
   rc = pthread_create(&sfthread1, NULL, hit_breakpoint_thread1, NULL);
   assert(rc == 0);
   rc = pthread_create(&sfthread2, NULL, hit_breakpoint_thread2, NULL);
   assert(rc == 0);

   starttime = time(NULL);
   while (true) {
      // Hit our breakpoints over and over again.
      t1 = hit_breakpoint1();
      t2 = hit_breakpoint2();
      if (((t1 + t2) / 2 - starttime) > RUNTIME) {
         break;
      }
      i++;
   }

   printf("%s ending, %d iterations\n", __FUNCTION__, i);

   return 0;
}
