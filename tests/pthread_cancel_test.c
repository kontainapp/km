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

struct timeval start;

void print_msg(char* m)
{
   struct timeval now;
   long d;

   if (greatest_get_verbosity() != 0) {
      gettimeofday(&now, NULL);
      if ((d = (now.tv_usec - start.tv_usec) / 1000) >= 0) {
         printf("%ld.%03ld: %s", now.tv_sec - start.tv_sec, d, m);
      } else {
         printf("%ld.%03ld: %s", now.tv_sec - start.tv_sec + 1, 1 - d, m);
      }
   }
}

static int thread_func(void* arg)
{
   int s;

   /* Disable cancellation for a while, so that we don't
      immediately react to a cancellation request */

   s = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
   ASSERT_EQ(0, s);
   print_msg("thread_func(): started; cancellation disabled\n");
   if (arg == NULL) {
      struct timeval now;

      mysleep(5);
      gettimeofday(&now, NULL);
      /* Check if we were interrupted midsleep */
      // TODO: musl bug - doesn't honor cancellation disabled state.
      // ASSERT_FALSE(now.tv_sec - start.tv_sec <= 3);
   }
   print_msg("thread_func(): about to enable cancellation\n");

   s = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   ASSERT_EQ(0, s);

   s = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   ASSERT_EQ(0, s);
   /* mysleep() is a cancellation point */
   mysleep(10); /* Should get canceled while we mysleep */

   /* Should never get here */
   FAIL();
}

TEST main_thread(void)
{
   pthread_t thr;
   void* res;
   int s;

   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request while it is canceldisable */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & thread_func, NULL)) != 0) {
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
   ASSERT_EQ(PTHREAD_CANCELED, res);

   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request cancel enable and in syscall */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & thread_func, (void*)1)) != 0) {
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
   ASSERT_EQ(PTHREAD_CANCELED, res);
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
