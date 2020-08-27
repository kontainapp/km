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
 */

/*
 * This program just creates a bunch of threads that recusively call a delay function
 * up to a set call depth and then return.
 * All of the test threads just keep doing this over and over until the
 * stop_running global becomes non-zero.
 * You can also use the -a command line argument to have the test run for a fixed
 * number of seconds.  For example "-a 5" to run the test for 5 seconds.
 *
 * Basically the test gives us something for gdb's "info threads" command to display.
 * And, the stack trace for each thread is moderately large so the gdb backtrace
 * command shows us something less mundane.
 */

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define MAX_THREADS	288	// KVM_MAX_VCPUS
#define DEFAULT_THREADS	10
#define MAX_DEPTH	10

int stop_running;
time_t starttime;
int stop_after_seconds;

pthread_mutex_t rand_serialize = PTHREAD_MUTEX_INITIALIZER;

void do_random_sleep(uint64_t instance_number, int *depth)
{
   struct timespec sleep_time;
   int rc;
   long int rn;

   rc = pthread_mutex_lock(&rand_serialize);
   assert(rc == 0);
   rn = random();
   rc = pthread_mutex_unlock(&rand_serialize);
   assert(rc == 0);

   (*depth)++;
   sleep_time.tv_sec = 0;
   sleep_time.tv_nsec = rn % 10000000;  // no more than 10ms
   rc = nanosleep(&sleep_time, NULL);
   if (rc != 0) {
      printf("thread instance %ld, nanosleep error %d\n", instance_number, rc);
   }
   if (stop_running == 0 && *depth < MAX_DEPTH) {
      do_random_sleep(instance_number, depth);
   }
   (*depth)--;
}

void* do_nothing_thread(void* instance)
{
   int depth;
   unsigned long iteration = 0;

   for (;;) {
      depth = 0;
      do_random_sleep((uint64_t)instance, &depth);
      iteration++;
      if (stop_running != 0) {
         break;
      }
      if (stop_after_seconds != 0 && (time(NULL) - starttime) > stop_after_seconds) {
         printf("Instance %ld stopping because time limit exceeded\n", (uint64_t)instance);
         break;
      }
   }
   return NULL;
}

void usage(void)
{
   printf("Usage: gdb_lots_of_threads [-a seconds] [-t thread_count]\n"
          "   -a = stop test after specified seconds, run forever if not specified\n"
          "   -t = create more threads than the default %d\n",
          DEFAULT_THREADS);
}

int main(int argc, char *argv[])
{
   long i;
   pthread_t threadid[MAX_THREADS];
   int max_threads = DEFAULT_THREADS;
   void* rv;
   struct timespec rs;
   int rc;

   for (int j = 1; j < argc; j++) {
      if (strcmp(argv[j], "-a") == 0) {
         if (j+1 >= argc) {
            usage();
            exit(1);
         }
         stop_after_seconds = atoi(argv[j+1]);
         printf("Stop running after %d seconds\n", stop_after_seconds);
         j++;
      } else if (strcmp(argv[j], "-t") == 0) {
         if (j+1 >= argc) {
            usage();
            exit(1);
         }
         max_threads = atoi(argv[j+1]);
         if (max_threads > MAX_THREADS) {
            printf("Number of threads can not exceed %d\n", MAX_THREADS);
            exit(1);
         }
         j++;
      } else {
         printf("Unknown argument %s\n", argv[j]);
         usage();
         exit(1);
      }
   }

   // Init random number generator for sleep times.
   rc = clock_gettime(CLOCK_REALTIME, &rs);
   assert(rc == 0);
   srandom(rs.tv_nsec);

   // start up the do nothing threads
   for (i = 0; i < max_threads; i++) {
      rc = pthread_create(&threadid[i], NULL, do_nothing_thread, (void*)i);
      if (rc != 0) {
         printf("Couldn't create thread instance %ld, error %s\n", i, strerror(rc));
         threadid[i] = 0;
      }
   }
   starttime = time(NULL);
   // Wait for threads to terminate
   for (i = 0; i < max_threads; i++) {
      if (threadid[i] != 0) {
         rc = pthread_join(threadid[i], &rv);
         if (rc != 0) {
            printf("Couldn't join thread instance %ld, error %s\n", i, strerror(rc));
         }
      }
   }
   return 0;
}
