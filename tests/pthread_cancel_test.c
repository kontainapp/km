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
 */

/*
 * Start a thread and then send it a cancellation request.
 * Two cases - PTHREAD_CANCEL_DISABLE and PTHREAD_CANCEL_ENABLE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "greatest/greatest.h"

#define handle_error_en(en, msg)                                                                   \
   do {                                                                                            \
      errno = en;                                                                                  \
      perror(msg);                                                                                 \
      exit(EXIT_FAILURE);                                                                          \
   } while (0)

long count;

struct timeval start;

void print_msg(char* m)
{
   struct timeval now;
   long d;

   if (greatest_get_verbosity() != 0) {
      gettimeofday(&now, NULL);
      d = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
      printf("%ld.%03ld: %s", d / 1000, d % 1000, m);
   }
}

long busy_loop(long i)
{
   volatile long x;
   for (; i > 0; i--) {
      x += i * i;
   }
   return x;
}

static const long calibrate_loops = 100000000l;
static const long nanosec = 1000000000l;

void calibrate_busy_sleep(void)
{
   struct timespec ts, ts_end;
   clock_gettime(CLOCK_REALTIME, &ts);
   busy_loop(calibrate_loops);
   clock_gettime(CLOCK_REALTIME, &ts_end);
   long cal_ns = (ts_end.tv_sec - ts.tv_sec) * nanosec + ts_end.tv_nsec - ts.tv_nsec;
   count = calibrate_loops * nanosec / cal_ns;
}

void my_busysleep(long c)
{
   busy_loop(count * c);
}

void mysleep(long c)
{
   fd_set rfds;
   struct timeval tv;

   FD_ZERO(&rfds);
   FD_SET(2, &rfds);

   tv.tv_sec = c;
   tv.tv_usec = 0;

   (void)select(1, &rfds, NULL, NULL, &tv);
}

#define DISABLE_CANCEL_TEST NULL
#define ASYNC_CANCEL_TEST (void*)1
#define DEFERRED_CANCEL_TEST (void*)2

static long thread_func(void* arg)
{
   int s;

   s = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
   ASSERT_EQ(0, s);
   print_msg("thread_func(): started; cancellation disabled\n");
   if (arg == DISABLE_CANCEL_TEST) {
      /* Disable cancellation for a while, so that we don't
         immediately react to a cancellation request */
      my_busysleep(5);
      print_msg("thread_func(): end of DISABLE_CANCEL_TEST\n");
   }
   print_msg("thread_func(): about to enable cancellation\n");

   s = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   ASSERT_EQ(0, s);

   s = pthread_setcanceltype(arg == DEFERRED_CANCEL_TEST ? PTHREAD_CANCEL_DEFERRED
                                                         : PTHREAD_CANCEL_ASYNCHRONOUS,
                             NULL);
   ASSERT_EQ(0, s);
   my_busysleep(5);
   /* Should get canceled while we sleep if ASYNC_CANCEL_TEST */
   print_msg(arg == DEFERRED_CANCEL_TEST ? "PTHREAD_CANCEL_DEFERRED\n" : "PTHREAD_CANCEL_ASYNCHRONOUS\n");
   usleep(1);
   pthread_exit((void*)0x17);   // Should never get here - 0x17 is a marker that we did get here
}

TEST main_thread(void)
{
   pthread_t thr;
   void* res;
   int s;

   calibrate_busy_sleep();
   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request while it is canceldisable */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & thread_func, DISABLE_CANCEL_TEST)) != 0) {
      handle_error_en(s, "pthread_create");
   }
   print_msg("main(): Give thread a chance to get started\n");

   mysleep(2); /* Give thread a chance to get started */

   print_msg("main(): sending cancellation request\n");
   s = pthread_cancel(thr);
   ASSERT_EQ(0, s);
   /* Join with thread to see what its exit status was */
   s = pthread_join(thr, &res);
   ASSERT_EQ(0, s);
   ASSERT_EQ_FMT(PTHREAD_CANCELED, res, "%p");

   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request cancel enable and sync cancellation */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & thread_func, ASYNC_CANCEL_TEST)) != 0) {
      handle_error_en(s, "pthread_create");
   }
   print_msg("main(): Give thread a chance to get started\n");

   mysleep(2); /* Give thread a chance to get started */

   print_msg("main(): sending cancellation request\n");
   s = pthread_cancel(thr);
   ASSERT_EQ(0, s);
   /* Join with thread to see what its exit status was */
   s = pthread_join(thr, &res);
   ASSERT_EQ(0, s);
   ASSERT_EQ_FMT(PTHREAD_CANCELED, res, "%p");

   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request cancel enable and deferred */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & thread_func, DEFERRED_CANCEL_TEST)) != 0) {
      handle_error_en(s, "pthread_create");
   }
   print_msg("main(): Give thread a chance to get started\n");

   mysleep(2); /* Give thread a chance to get started */

   print_msg("main(): sending cancellation request\n");
   s = pthread_cancel(thr);
   ASSERT_EQ(0, s);
   /* Join with thread to see what its exit status was */
   s = pthread_join(thr, &res);
   ASSERT_EQ(0, s);
   ASSERT_EQ_FMT(PTHREAD_CANCELED, res, "%p");

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args

   RUN_TEST(main_thread);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}
