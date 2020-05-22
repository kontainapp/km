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
 * Simple test for snapshots. Two threads, main and a worker. Worker initiates
 * snapshot. Ensure state from prior to the snapshot is restored properly.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "km_hcalls.h"

char* cmdname = "???";
int do_abort = 0;
int no_snap = 0;

pthread_mutex_t long_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
int flag = 0;
int zerofd = -1;   // fd to '/dev/zero'
int filefd = -1;   // fd to '/etc/passwd' O_RDONLY

typedef struct process_state {
   int zerofail;         // zerofd stat failed
   struct stat zerost;   // zerofd stat
   int filefail;         // filefd stat failed
   struct stat filest;   // filefd stat
   off_t fileoff;
} process_state_t;

process_state_t presnap;
process_state_t postsnap;

void setup_process_state()
{
   zerofd = open("/dev/zero", O_RDONLY);
   filefd = open("/etc/passwd", O_RDONLY);
   lseek(filefd, 100, SEEK_SET);
   return;
}

void teardown_process_state()
{
   close(filefd);
   close(zerofd);
}

void get_process_state(process_state_t* state)
{
   if (fstat(zerofd, &state->zerost) < 0) {
      state->zerofail = 1;
      perror("fstat zerofd");
   }
   if (fstat(filefd, &state->filest) < 0) {
      state->filefail = 1;
      perror("fstat filefd");
   }
   if ((state->fileoff = lseek(filefd, 0, SEEK_CUR)) == -1) {
      perror("lseek filefd");
   }
   return;
}

int compare_process_state(process_state_t* prestate, process_state_t* poststate)
{
   int ret = 0;
   if (prestate->zerofail != 0 || prestate->filefail != 0) {
      fprintf(stderr, "prestate fstat failure");
      ret = -1;
   }
   if (prestate->zerofail != 0 || prestate->filefail != 0) {
      fprintf(stderr, "postestate fstat failure");
      ret = -1;
   }
   if (prestate->zerost.st_mode != poststate->zerost.st_mode) {
      fprintf(stderr, "/dev/zero mode changed\n");
      ret = -1;
   }
   if (prestate->filest.st_mode != poststate->filest.st_mode) {
      fprintf(stderr, "/etc/passwd mode changed\n");
      ret = -1;
   }
   if (prestate->fileoff != poststate->fileoff) {
      fprintf(stderr, "/etc/passwd offset changed\n");
      ret = -1;
   }
   return ret;
}

void usage()
{
   fprintf(stderr, "%s [-a]\n", cmdname);
}

static inline void block()
{
   pthread_mutex_lock(&lock);
   while (flag == 0) {
      pthread_cond_wait(&cond, &lock);
   }
   pthread_mutex_unlock(&lock);
}

static inline void wakeup()
{
   pthread_mutex_lock(&lock);
   flag = 1;
   pthread_cond_broadcast(&cond);
   pthread_mutex_unlock(&lock);
}

/*
 * Subthread to take snapshot. After snapshot, ensure mutex state
 * is restored and wake up the main thread.
 */
void* thread_main(void* arg)
{
   /*
    * Take the snapshot.
    */
   if (no_snap == 0) {
      km_hc_args_t snapshotargs = {};
      km_hcall(HC_snapshot, &snapshotargs);
   }

   /*
    * Snapshot resumes here.
    * - long_lock should still be held by main thread.
    */
   if (pthread_mutex_trylock(&long_lock) == 0) {
      fprintf(stderr, "long_lock state not restored\n");
      exit(1);
   }
   wakeup();

   fprintf(stderr, "Hello from thread\n");
   return NULL;
}

int main(int argc, char* argv[])
{
   int c;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "an")) != -1) {
      switch (c) {
         case 'a':
            do_abort = 1;
            break;

         case 'n':
            no_snap = 1;
            break;

         default:
            usage();
            break;
      }
   }

   setup_process_state();
   get_process_state(&presnap);

   pthread_mutex_lock(&long_lock);

   pthread_t thr;
   if (pthread_create(&thr, NULL, thread_main, NULL) != 0) {
      perror("pthread_create");
      return 1;
   }

   block();

   void* rval;
   if (pthread_join(thr, &rval) != 0) {
      perror("pthread_join");
      return 1;
   }

   get_process_state(&postsnap);
   if (compare_process_state(&presnap, &postsnap) != 0) {
      fprintf(stderr, "!!! state restoration error !!!\n");
   }

   if (do_abort != 0) {
      abort();
   }
   pthread_mutex_unlock(&long_lock);
   fprintf(stderr, "Success\n");
   return 0;
}