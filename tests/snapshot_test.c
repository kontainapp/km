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

#define _GNU_SOURCE
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
int socketonlyfd = -1;
int socketbindfd = -1;
int socketlistenfd = -1;
int socketconnected_end0_fd = -1;
int socketconnected_end1_fd = -1;
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
   struct stat socketonlyst;
   struct stat socketbindst;              // socket(2) stat
   struct stat socketlistenst;            // socket(2) stat
   struct stat socketconnected_end0_st;   // socket(2) stat
   struct stat socketconnected_end1_st;   // socket(2) stat
   struct stat socdupst;                  // socket(2) dup stat
   struct stat epollst;                   // epoll_create stat
   struct stat eventfdst;
   struct stat dup_st[5];
   socklen_t socketbind_socklen;
   struct sockaddr socketbind_sockname;
   socklen_t socketlisten_socklen;
   struct sockaddr socketlisten_sockname;
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

   // Create a socket with nothing done to it
   CHECK_SYSCALL(socketonlyfd = socket(AF_INET, SOCK_STREAM, 0));

   // Create a socket with local address bound
   CHECK_SYSCALL(socketbindfd = socket(AF_INET, SOCK_STREAM, 0));
   int flag = 1;
   CHECK_SYSCALL(setsockopt(socketbindfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)));
   struct sockaddr_in saddr;
   saddr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
   saddr.sin_port = htons(port);
   CHECK_SYSCALL(bind(socketbindfd, (struct sockaddr*)&saddr, sizeof(saddr)));

   // Create a socket that is listening
   CHECK_SYSCALL(socketlistenfd = socket(AF_INET, SOCK_STREAM, 0));
   flag = 1;
   CHECK_SYSCALL(setsockopt(socketlistenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)));
   saddr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
   saddr.sin_port = htons(port + 1);
   CHECK_SYSCALL(bind(socketlistenfd, (struct sockaddr*)&saddr, sizeof(saddr)));
   CHECK_SYSCALL(listen(socketlistenfd, 1));

   // Now create a pair of fd's for each end of a connection.
   CHECK_SYSCALL(socketconnected_end0_fd = socket(AF_INET, SOCK_STREAM, 0));
   CHECK_SYSCALL(fcntl(socketconnected_end0_fd, F_SETFL, O_NONBLOCK));
   struct sockaddr_in connect_sa;
   int connect_socklen = sizeof(connect_sa);
   connect_sa.sin_family = AF_INET;
   connect_sa.sin_port = htons(port + 1);
   connect_sa.sin_addr.s_addr = inet_addr("127.0.0.1");
   // initiate the connect
   if (connect(socketconnected_end0_fd, &connect_sa, connect_socklen) < 0) {
      if (errno != EINPROGRESS) {
         fprintf(stderr, "Couldn't connect, %s\n", strerror(errno));
         exit(100);
      }
   }
   struct sockaddr_in accepted_sa;
   socklen_t accepted_socklen = sizeof(accepted_sa);
   CHECK_SYSCALL(socketconnected_end1_fd = accept(socketlistenfd, &accepted_sa, &accepted_socklen));
   // finish off the connect
   CHECK_SYSCALL(connect(socketconnected_end0_fd, &connect_sa, connect_socklen));

   CHECK_SYSCALL(socdupfd = dup(socketbindfd));
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
   CHECK_SYSCALL(fstat(socketonlyfd, &state->socketonlyst));

   CHECK_SYSCALL(fstat(socketbindfd, &state->socketbindst));
   state->socketbind_socklen = sizeof(state->socketbind_sockname);
   CHECK_SYSCALL(getsockname(socketbindfd, &state->socketbind_sockname, &state->socketbind_socklen));

   CHECK_SYSCALL(fstat(socketlistenfd, &state->socketlistenst));
   state->socketlisten_socklen = sizeof(state->socketlisten_sockname);
   CHECK_SYSCALL(
       getsockname(socketlistenfd, &state->socketlisten_sockname, &state->socketlisten_socklen));

   CHECK_SYSCALL(fstat(socketconnected_end0_fd, &state->socketconnected_end0_st));
   CHECK_SYSCALL(fstat(socketconnected_end1_fd, &state->socketconnected_end1_st));
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
   if (prestate->socketonlyst.st_mode != poststate->socketonlyst.st_mode) {
      fprintf(stderr, "socketonly changed\n");
      ret = -1;
   }
   if (prestate->socketbindst.st_mode != poststate->socketbindst.st_mode ||
       poststate->socdupst.st_mode != poststate->socketbindst.st_mode) {
      fprintf(stderr,
              "socket changed 0x%x 0x%x 0x%x\n",
              prestate->socketbindst.st_mode,
              poststate->socketbindst.st_mode,
              poststate->socdupst.st_mode);
      ret = -1;
   }
   if (prestate->socketbind_socklen != poststate->socketbind_socklen ||
       memcmp(&prestate->socketbind_sockname,
              &poststate->socketbind_sockname,
              prestate->socketbind_socklen) != 0) {
      fprintf(stderr,
              "socketbindfd: sockname differences (socklen pre %d, post %d), (sockname pre "
              "%d:%d:0x%x post %d:%d:0x%x)\n",
              prestate->socketbind_socklen,
              poststate->socketbind_socklen,
              ((struct sockaddr_in*)&prestate->socketbind_sockname)->sin_family,
              ((struct sockaddr_in*)&prestate->socketbind_sockname)->sin_port,
              ((struct sockaddr_in*)&prestate->socketbind_sockname)->sin_addr.s_addr,
              ((struct sockaddr_in*)&poststate->socketbind_sockname)->sin_family,
              ((struct sockaddr_in*)&poststate->socketbind_sockname)->sin_port,
              ((struct sockaddr_in*)&poststate->socketbind_sockname)->sin_addr.s_addr);
      ret = -1;
   }
   if (prestate->socketlistenst.st_mode != poststate->socketlistenst.st_mode) {
      fprintf(stderr, "socketlisten changed\n");
      ret = -1;
   }
   if (prestate->socketlisten_socklen != poststate->socketlisten_socklen ||
       memcmp(&prestate->socketlisten_sockname,
              &poststate->socketlisten_sockname,
              prestate->socketlisten_socklen) != 0) {
      fprintf(stderr,
              "socketlistenfd: sockname differences (socklen pre %d, post %d), (sockname pre "
              "%d:%d:0x%x post %d:%d:0x%x)\n",
              prestate->socketlisten_socklen,
              poststate->socketlisten_socklen,
              ((struct sockaddr_in*)&prestate->socketlisten_sockname)->sin_family,
              ((struct sockaddr_in*)&prestate->socketlisten_sockname)->sin_port,
              ((struct sockaddr_in*)&prestate->socketlisten_sockname)->sin_addr.s_addr,
              ((struct sockaddr_in*)&poststate->socketlisten_sockname)->sin_family,
              ((struct sockaddr_in*)&poststate->socketlisten_sockname)->sin_port,
              ((struct sockaddr_in*)&poststate->socketlisten_sockname)->sin_addr.s_addr);
      ret = -1;
   }
   if (prestate->socketconnected_end0_st.st_mode != poststate->socketconnected_end0_st.st_mode) {
      fprintf(stderr, "socketconnected_end0 changed\n");
      ret = -1;
   }
   if (prestate->socketconnected_end1_st.st_mode != poststate->socketconnected_end1_st.st_mode) {
      fprintf(stderr, "socketconnected_end1 changed\n");
      ret = -1;
   }
   // Verify that the connected socket fd's are now no longer connected.
   int rc;
   char buf[16];
   rc = recv(socketconnected_end0_fd, buf, sizeof(buf), MSG_DONTWAIT);
   if (rc >= 0 || errno != ECONNRESET) {
      fprintf(stderr, "connected socket 0 is still connected after snapshot, rc %d, errno %d\n", rc, errno);
      ret = -1;
   }
   rc = recv(socketconnected_end1_fd, buf, sizeof(buf), MSG_DONTWAIT);
   if (rc >= 0 || errno != ECONNRESET) {
      fprintf(stderr, "connected socket 1 is still connected after snapshot, rc %d, errno %d\n", rc, errno);
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

   char buf[1024];
   km_hc_args_t getdata_args = {.arg1 = (uintptr_t)buf, .arg2 = sizeof(buf)};
   km_hcall(HC_snapshot_getdata, &getdata_args);
   fprintf(stderr, "getdata rc=%ld\n", getdata_args.hc_ret);

   km_hc_args_t putdata_args = {.arg1 = (uintptr_t)buf, .arg2 = getdata_args.hc_ret};
   km_hcall(HC_snapshot_putdata, &putdata_args);
   fprintf(stderr, "putdata rc=%ld\n", putdata_args.hc_ret);

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
