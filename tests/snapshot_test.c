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
#define SNAP_NORMAL 0
#define SNAP_NONE 1
#define SNAP_PAUSE 2
int snap_flag = SNAP_NORMAL;

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

/*
 * Compare prestate (gathered before the snapshot) and poststate (gathered
 * after the snapshot). Returns 0 is OK, -12 if mismatch.
 */
int compare_process_state(process_state_t* prestate, process_state_t* poststate)
{
   int ret = 0;
   if (prestate->zerofail != 0 || prestate->filefail != 0) {
      fprintf(stderr, "prestate fstat failure");
      ret = -1;
   }
   if (poststate->zerofail != 0 || poststate->filefail != 0) {
      fprintf(stderr, "postestate fstat failure");
      ret = -1;
   }
   if (prestate->zerost.st_mode != poststate->zerost.st_mode) {
      fprintf(stderr,
              "/dev/zero mode changed: expect=%o got=%o\n",
              prestate->zerost.st_mode,
              poststate->zerost.st_mode);
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
   if (snap_flag == SNAP_NORMAL) {
      km_hc_args_t snapshotargs = {};
      km_hcall(HC_snapshot, &snapshotargs);
   } else if (snap_flag == SNAP_PAUSE) {
      pause();
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

void usage()
{
   fprintf(stderr, "%s [-a] [-sn] [-sp]\n", cmdname);
   fprintf(stderr, " -a  Abort after snapshot resume\n");
   fprintf(stderr, " -sn Don't create snapshot. Runs to completion\n");
   fprintf(stderr, " -sp pause instead of creating snapshot. Runs to completion\n");
}

int main(int argc, char* argv[])
{
   int c;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "anp")) != -1) {
      switch (c) {
         case 'a':
            do_abort = 1;
            break;

         case 'n':
            snap_flag = SNAP_NONE;
            break;

         case 'p':
            snap_flag = SNAP_PAUSE;
            break;

         default:
            usage();
            break;
      }
   }

   setup_process_state();

   // Gather state prior to snapshot.
   get_process_state(&presnap);

   pthread_mutex_lock(&long_lock);

   pthread_t thr;
   /*
    * Everything prior to here is
    * unquestionably prior to the snapshot resume.
    */
   if (pthread_create(&thr, NULL, thread_main, NULL) != 0) {
      perror("pthread_create");
      return 1;
   }

   /*
    * when code is here it's state is ambiguous WRT the snapshot resume.
    */
   block();
   /*
    * Everything after here is unquestionably in the snapshot resume.
    */

   void* rval;
   if (pthread_join(thr, &rval) != 0) {
      perror("pthread_join");
      return 1;
   }

   /*
    * get state after snapshot resume and insure it matches state
    * prior to resume.
    */
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