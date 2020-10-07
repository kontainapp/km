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
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/futex.h>

#include "km_hcalls.h"

char* cmdname = "???";
int do_abort = 0;
int do_stdclose = 0;
#define SNAP_NORMAL 0
#define SNAP_NONE 1
#define SNAP_PAUSE 2
int snap_flag = SNAP_NORMAL;

pthread_mutex_t long_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
int woken = 0;
int zerofd = -1;   // fd to '/dev/zero'
int filefd = -1;   // fd to '/etc/passwd' O_RDONLY
int pipefd[2] = {1, -1};
int spairfd[2] = {1, -1};
int socketfd = -1;
int epollfd = -1;

typedef struct process_state {
   int zerofail;         // zerofd stat failed
   struct stat zerost;   // zerofd stat
   int filefail;         // filefd stat failed
   struct stat filest;   // filefd stat
   off_t fileoff;
   struct stat pipest[2];    // pipe stat
   struct stat spairst[2];   // socketpair stat
   struct stat socketst;     // socket(2) stat
   struct stat epollst;      // epoll_create stat
} process_state_t;

process_state_t presnap;
process_state_t postsnap;

#define CHECK_SYSCALL(x)                                                                           \
   {                                                                                               \
      fprintf(stderr, #x "\n");                                                                    \
      if ((x) < 0) {                                                                               \
         fprintf(stderr, "error line %d error %d - exiting\n", __LINE__, errno);                   \
         abort();                                                                                  \
      }                                                                                            \
   }

void setup_process_state()
{
   int tmpfd;
   CHECK_SYSCALL(tmpfd = open("/dev/zero", O_RDONLY));
   CHECK_SYSCALL(zerofd = open("/dev/zero", O_RDONLY));
   CHECK_SYSCALL(zerofd = open("/dev/zero", O_RDONLY));
   CHECK_SYSCALL(filefd = open("/etc/passwd", O_RDONLY));
   CHECK_SYSCALL(lseek(filefd, 100, SEEK_SET));
   CHECK_SYSCALL(pipe(pipefd));
   CHECK_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, spairfd));
   CHECK_SYSCALL(socketfd = socket(AF_INET, SOCK_STREAM, 0));
   CHECK_SYSCALL(epollfd = epoll_create(1));
   // leave a gap in FS space for recovery
   CHECK_SYSCALL(close(tmpfd));
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
      fprintf(stderr, "fstat zerofd %d, %s\n", zerofd, strerror(errno));
   }
   if (fstat(filefd, &state->filest) < 0) {
      state->filefail = 1;
      fprintf(stderr, "fstat filefd %d, %s\n", filefd, strerror(errno));
   }
   CHECK_SYSCALL(state->fileoff = lseek(filefd, 0, SEEK_CUR));
   CHECK_SYSCALL(fstat(pipefd[0], &state->pipest[0]));
   CHECK_SYSCALL(fstat(pipefd[1], &state->pipest[1]));
   CHECK_SYSCALL(fstat(spairfd[0], &state->spairst[0]));
   CHECK_SYSCALL(fstat(spairfd[1], &state->spairst[1]));
   CHECK_SYSCALL(fstat(socketfd, &state->socketst));
   CHECK_SYSCALL(fstat(epollfd, &state->epollst));
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

   // pipe
   if (prestate->pipest[0].st_mode != poststate->pipest[0].st_mode) {
      fprintf(stderr, "pipe[0]changed\n");
      ret = -1;
   }
   if (prestate->pipest[1].st_mode != poststate->pipest[1].st_mode) {
      fprintf(stderr, "pipe[1]changed\n");
      ret = -1;
   }
   // Socket pair
   if (prestate->spairst[0].st_mode != poststate->spairst[0].st_mode) {
      fprintf(stderr, "socketpair[0]changed\n");
      ret = -1;
   }
   if (prestate->spairst[1].st_mode != poststate->spairst[1].st_mode) {
      fprintf(stderr, "socketpair[1]changed\n");
      ret = -1;
   }
   if (prestate->socketst.st_mode != poststate->socketst.st_mode) {
      fprintf(stderr, "socket changed\n");
      ret = -1;
   }
   if (prestate->epollst.st_mode != poststate->epollst.st_mode) {
      fprintf(stderr, "epoll changed\n");
      ret = -1;
   }
   return ret;
}

static inline void block()
{
   pthread_mutex_lock(&lock);
   while (woken == 0) {
      pthread_cond_wait(&cond, &lock);
   }
   pthread_mutex_unlock(&lock);
}

static inline void wakeup()
{
   pthread_mutex_lock(&lock);
   woken = 1;
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
   while ((c = getopt(argc, argv, "anpc")) != -1) {
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

         case 'c':
            do_stdclose = 1;
            break;

         default:
            usage();
            break;
      }
   }

   setup_process_state();

   if (do_stdclose != 0) {
      fclose(stdin);
      fclose(stdout);
      fclose(stderr);
   }

   // Gather state prior to snapshot.
   get_process_state(&presnap);

   pthread_mutex_lock(&long_lock);

   pthread_t thr;
   /*
    * Everything prior to here is
    * unquestionably prior to the snapshot resume.
    */
   if ((c = pthread_create(&thr, NULL, thread_main, NULL)) != 0) {
      fprintf(stderr, "pthread_create %s:%d, %s\n", __FILE__, __LINE__, strerror(c));
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
   if ((c = pthread_join(thr, &rval)) != 0) {
      fprintf(stderr, "pthread_join %s:%d, %s\n", __FILE__, __LINE__, strerror(c));
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