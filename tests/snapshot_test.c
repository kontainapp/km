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
 * This program generates exceptions in guest in order to test guest coredumps
 * and other processing.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "km_hcalls.h"

char* cmdname = "???";
int do_abort = 0;

void usage()
{
   fprintf(stderr, "%s [-a]\n", cmdname);
}

pthread_mutex_t lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
int flag = 0;

// Perform a snapshot
void* thread_main(void* arg)
{
   km_hc_args_t snapshotargs = {};
   km_hcall(HC_snapshot, &snapshotargs);

   pthread_mutex_lock(&lock);
   flag = 1;
   pthread_cond_broadcast(&cond);
   pthread_mutex_unlock(&lock);

   fprintf(stderr, "Hello from thread\n");
   return NULL;
}

int main(int argc, char* argv[])
{
   int c;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "a")) != -1) {
      switch (c) {
         case 'a':
            do_abort = 1;
            break;

         default:
            usage();
            break;
      }
   }
   pthread_t thr;
   if (pthread_create(&thr, NULL, thread_main, NULL) != 0) {
      perror("pthread_create");
      return 1;
   }

   pthread_mutex_lock(&lock);
   while (flag == 0) {
      pthread_cond_wait(&cond, &lock);
   }
   pthread_mutex_unlock(&lock);

   void* rval;
   if (pthread_join(thr, &rval) != 0) {
      perror("pthread_join");
      return 1;
   }
   if (do_abort != 0) {
      abort();
   }
   fprintf(stderr, "Success\n");
   return 0;
}