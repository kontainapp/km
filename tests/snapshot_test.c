/*
 * Copyright 2021 Kontain Inc
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
 * Simple test for snapshots. Two threads, main and a worker. Worker initiates
 * snapshot. Ensure state from prior to the snapshot is restored properly.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <linux/futex.h>
#include <netinet/in.h>

#include "km_hcalls.h"

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

int port = 6565;

char* cmdname = "???";
int do_abort = 0;
int do_stdclose = 0;
#define SNAP_NORMAL 0
#define SNAP_NONE 1
#define SNAP_PAUSE 2
#define SNAP_LIVE 3
int snap_flag = SNAP_NORMAL;

pthread_mutex_t long_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
int woken = 0;
int zerofd = -1;   // fd to '/dev/zero'
int filefd = -1;   // fd to '/etc/passwd' O_RDONLY
int sec_fd = -1;   // fd to '/etc/passwd' O_RDONLY
int pipefd[2] = {1, -1};
int spairfd[2] = {1, -1};
int socketfd = -1;
int socdupfd = -1;
int epollfd = -1;
int eventfd_fd = -1;

int dup_fd[5];

typedef struct process_state {
   int zerofail;         // zerofd stat failed
   struct stat zerost;   // zerofd stat
   int filefail;         // filefd stat failed
   struct stat filest;   // filefd stat
   struct stat fdupst;   // sec_fd stat
   off_t fileoff;
   struct stat pipest[2];    // pipe stat
   struct stat spairst[2];   // socketpair stat
   struct stat socketst;     // socket(2) stat
   struct stat socdupst;     // socket(2) dup stat
   struct stat epollst;      // epoll_create stat
   struct stat eventfdst;
   struct stat dup_st[5];
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

pthread_mutex_t signal_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t signal_cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
int signal_seen = 0;
void test_sigaction(int signo, siginfo_t* info, void* ctx)
{
   pthread_mutex_lock(&signal_lock);
   signal_seen++;
   pthread_cond_broadcast(&signal_cond);
   pthread_mutex_unlock(&signal_lock);
}

static int secret = 1;
void clear_secret(int signo)
{
   fprintf(stderr, "clearing secret\n");
   secret = 0;
}

void restore_secret(int signo)
{
   if (secret != 0) {
      fprintf(stderr, "secret was not cleared before snapshot\n");
      exit(1);
   }
   secret = 2;
}

void setup_process_state()
{
   int tmpfd;
   CHECK_SYSCALL(tmpfd = open("/dev/zero", O_RDONLY));
   CHECK_SYSCALL(zerofd = open("/dev/zero", O_RDONLY));
   CHECK_SYSCALL(zerofd = open("/dev/zero", O_RDONLY));   // are we intentionally leaking an fd here?
   CHECK_SYSCALL(filefd = open("/etc/passwd", O_RDONLY));
   CHECK_SYSCALL(sec_fd = open("/etc/passwd", O_RDONLY))
   CHECK_SYSCALL(lseek(filefd, 100, SEEK_SET));
   CHECK_SYSCALL(pipe(pipefd));
   CHECK_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, spairfd));
   CHECK_SYSCALL(socketfd = socket(AF_INET, SOCK_STREAM, 0));
   int flag = 1;
   CHECK_SYSCALL(setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)));
   struct sockaddr_in saddr;
   saddr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
   saddr.sin_port = port;
   CHECK_SYSCALL(bind(socketfd, (struct sockaddr*)&saddr, sizeof(saddr)));
   CHECK_SYSCALL(socdupfd = dup(socketfd));
   CHECK_SYSCALL(epollfd = epoll_create(1));
   CHECK_SYSCALL(eventfd_fd = eventfd(99, 0));
   // leave a gap in FS space for recovery
   CHECK_SYSCALL(close(tmpfd));

   CHECK_SYSCALL(dup_fd[0] = open("/etc/passwd", O_RDONLY));
   CHECK_SYSCALL(dup_fd[1] = dup(dup_fd[0]));
   CHECK_SYSCALL(dup_fd[2] = fcntl(dup_fd[0], F_DUPFD, 0));
   close(dup_fd[1]);

   CHECK_SYSCALL(dup_fd[3] = open("/etc/passwd", O_RDONLY));
   CHECK_SYSCALL(dup_fd[4] = dup(dup_fd[3]));

   CHECK_SYSCALL(dup_fd[1] = dup(dup_fd[2]));

   struct sigaction act = {
       .sa_sigaction = test_sigaction,
       .sa_flags = SA_SIGINFO,
   };
   CHECK_SYSCALL(sigaction(SIGUSR1, &act, NULL));

   {
      struct sigaction act = {
          .sa_handler = clear_secret,
          .sa_flags = SA_SIGINFO,
      };
      CHECK_SYSCALL(syscall(SYS_rt_sigaction, KM_SIGSNAPCREATE, &act, NULL, sizeof(sigset_t)));
   }
   {
      struct sigaction act = {
          .sa_handler = restore_secret,
          .sa_flags = SA_SIGINFO,
      };
      CHECK_SYSCALL(syscall(SYS_rt_sigaction, KM_SIGSNAPRESTORE, &act, NULL, sizeof(sigset_t)));
   }

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
   if (fstat(sec_fd, &state->fdupst) < 0) {
      state->filefail = 1;
      fprintf(stderr, "fstat filefd %d, %s\n", sec_fd, strerror(errno));
   }
   CHECK_SYSCALL(state->fileoff = lseek(filefd, 0, SEEK_CUR));
   CHECK_SYSCALL(fstat(pipefd[0], &state->pipest[0]));
   CHECK_SYSCALL(fstat(pipefd[1], &state->pipest[1]));
   CHECK_SYSCALL(fstat(spairfd[0], &state->spairst[0]));
   CHECK_SYSCALL(fstat(spairfd[1], &state->spairst[1]));
   CHECK_SYSCALL(fstat(socketfd, &state->socketst));
   CHECK_SYSCALL(fstat(socdupfd, &state->socdupst));
   CHECK_SYSCALL(fstat(epollfd, &state->epollst));
   CHECK_SYSCALL(fstat(eventfd_fd, &state->eventfdst));
   for (int i = 0; i < 5; i++) {
      CHECK_SYSCALL(fstat(dup_fd[i], &state->dup_st[i]));
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
      fprintf(stderr, "prestate fstat failure\n");
      ret = -1;
   }
   if (poststate->zerofail != 0 || poststate->filefail != 0) {
      fprintf(stderr, "poststate fstat failure\n");
      ret = -1;
   }
   if (prestate->zerost.st_mode != poststate->zerost.st_mode) {
      fprintf(stderr,
              "/dev/zero mode changed: expect=%o got=%o\n",
              prestate->zerost.st_mode,
              poststate->zerost.st_mode);
      ret = -1;
   }
   if (prestate->filest.st_mode != poststate->filest.st_mode ||
       poststate->fdupst.st_mode != poststate->filest.st_mode) {
      fprintf(stderr, "/etc/passwd mode changed\n");
      ret = -1;
   }
   if (prestate->fileoff != poststate->fileoff) {
      fprintf(stderr, "/etc/passwd offset changed\n");
      ret = -1;
   }
   // check sec_fd off doesn't move with filefd
   lseek(sec_fd, 1, SEEK_CUR);
   if (lseek(filefd, 0, SEEK_CUR) == lseek(sec_fd, 0, SEEK_CUR)) {
      fprintf(stderr, "/etc/passwd offset followed\n");
      ret = -1;
   }

   if (prestate->dup_st[0].st_mode != poststate->dup_st[0].st_mode ||
       prestate->dup_st[1].st_mode != poststate->dup_st[1].st_mode ||
       prestate->dup_st[2].st_mode != poststate->dup_st[2].st_mode) {
      fprintf(stderr, "dup 0 1 2 file mode changed\n");
      ret = -1;
   }

   if (prestate->dup_st[3].st_mode != poststate->dup_st[3].st_mode ||
       prestate->dup_st[3].st_mode != poststate->dup_st[4].st_mode) {
      fprintf(stderr, "dup 3 4 file mode changed\n");
      ret = -1;
   }

   lseek(dup_fd[0], 1, SEEK_CUR);
   if (lseek(dup_fd[0], 0, SEEK_CUR) != lseek(dup_fd[1], 0, SEEK_CUR) ||
       lseek(dup_fd[0], 0, SEEK_CUR) != lseek(dup_fd[2], 0, SEEK_CUR)) {
      fprintf(stderr, "/etc/passwd didn't follow\n");
      ret = -1;
   }
   if (lseek(dup_fd[3], 0, SEEK_CUR) == lseek(dup_fd[2], 0, SEEK_CUR)) {
      fprintf(stderr, "/etc/passwd followed\n");
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
   if (prestate->socketst.st_mode != poststate->socketst.st_mode ||
       poststate->socdupst.st_mode != poststate->socketst.st_mode) {
      fprintf(stderr,
              "socket changed 0x%x 0x%x 0x%x\n",
              prestate->socketst.st_mode,
              poststate->socketst.st_mode,
              poststate->socdupst.st_mode);
      ret = -1;
   }
   if (prestate->epollst.st_mode != poststate->epollst.st_mode) {
      fprintf(stderr, "epoll changed\n");
      ret = -1;
   }
   if (prestate->eventfdst.st_mode != poststate->eventfdst.st_mode) {
      fprintf(stderr, "eventfd changed\n");
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
   double a1 = 99.99;
   register double a2 asm("xmm7") = 101.202;

   uint64_t seventeen = 17;
   km_hc_args_t snapshotargs = {.arg1 = (uint64_t) "snaptest_label",
                                .arg2 = (uint64_t) "Snapshot test application",
                                .arg3 = (snap_flag == SNAP_LIVE) ? 1 : 0};
   // Dirty XMM0.
   // This needs to be right before km_hcall() to make sure xmm0 is intact at the moment of snapshot
   asm volatile("movq %0, %%xmm0"
                : /* No output */
                : "r"(seventeen)
                : "%xmm0");

   /*
    * Take the snapshot.
    */
   if (snap_flag == SNAP_NORMAL || snap_flag == SNAP_LIVE) {
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

   if (secret != 2) {
      fprintf(stderr, "secret was not updated before restore\n");
      exit(1);
   }

   /*
    * Ensure floating point state was restored.
    */
   uint64_t val = -1;
   asm volatile("movq %%xmm0, %0"
                : "=r"(val)
                : /* No input */
                :);

   if (val != seventeen || a1 != 99.99 || a2 != 101.202) {
      fprintf(stderr, "ERROR: FP not restored: %ld, %g %g\n", val, a1, a2);
      exit(1);
   }
   wakeup();

   fprintf(stderr, "Hello from thread\n");
   return NULL;
}

void usage()
{
   fprintf(stderr, "%s [-a] [-sn] [-sp] port\n", cmdname);
   fprintf(stderr, " -a  Abort after snapshot resume\n");
   fprintf(stderr, " -sn Don't create snapshot. Runs to completion\n");
   fprintf(stderr, " -sp pause instead of creating snapshot. Runs to completion\n");
}

int main(int argc, char* argv[])
{
   int c;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "anplc")) != -1) {
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

         case 'l':
            snap_flag = SNAP_LIVE;
            break;

         case 'c':
            do_stdclose = 1;
            break;

         default:
            usage();
            break;
      }
   }
   if (optind < argc) {
      port = atoi(argv[optind]);
   }

   setup_process_state();

   // make sure signal handler uses a restorer (MUSL)
   struct sigaction oldact = {};
   CHECK_SYSCALL(sigaction(SIGUSR1, NULL, &oldact));
   if ((oldact.sa_flags & SA_RESTORER) == 0) {
      fprintf(stderr, "No signal restorer\n");
      return 1;
   }

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

   kill(0, SIGUSR1);
   pthread_mutex_lock(&signal_lock);
   while (signal_seen != 1) {
      pthread_cond_wait(&signal_cond, &signal_lock);
   }
   pthread_mutex_unlock(&signal_lock);

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
