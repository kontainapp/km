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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "km_hcalls.h"

// Some tests need a network port which is to be provided via the environment variable named here.
char* SOCKET_PORT = "SOCKET_PORT";

/*
 * A simple test to verify that snapshots fail if the following are found in the
 * payload being snapshotted.
 * - epoll fd's with events
 * - sockets that are in a connected state.
 * - interval timers that are running (setitimer() and timer_create())
 * Now the following conditions are supported.  So test that they work.
 * - pipes with queued data
 * - socketpair's with queued data
 * We can try each of these things with different command line options.
 * We verify success by this program exiting with an error status and an error message.
 * Command line flags:
 *  -e - eventfd
 *  -p - pipe
 *  -h - pipe with write end closed
 *  -c - connected socket
 *  -s - socketpair
 *  -m - socketpair with one end closed
 *  -t - active interval timers (setitimer() type timers)
 *  -f - verify that snapshot is not allowed after a process has forked.
 *  -z - timer_create() type interval timers (no support for this yet)
 * Use only one flag at a time.

 * Note that we expect taking the snapshot to fail for these tests and the snapshot
 * hypercall doesn't return on failure, km just exits with a return code.
 * When this test fails for other reasons it is important that it not fail with
 * the same exit values km uses for snapshot failures.
 * We have arbitrarily chosen to have this program use the exit value 100 to indicate
 * it failed for reasons of its own.
 */
void usage(void)
{
   fprintf(stderr, "Usage: snapshot_fail_test [ -e | -p | -c | -s | -t | -z ]\n");
   fprintf(stderr, "       -e = epoll fd pending events\n");
   fprintf(stderr, "       -f = snapshot fails after process forks\n");
   fprintf(stderr, "       -p = pipe with queued data\n");
   fprintf(stderr, "       -h = half open pipe with queued data\n");
   fprintf(stderr, "       -c = connected socket\n");
   fprintf(stderr, "       -s = socketpair with queued data\n");
   fprintf(stderr, "       -m = half open socketpair with queued data\n");
   fprintf(stderr, "       -t = active setitimer interval timer\n");
   fprintf(stderr, "       -z = active timer_create interval timer (km does not support these yet)\n");
}

/*
 * Parameters:
 *  halfopen - if not zero, then close the write end of the pipe before
 *    taking the snapshot.  Otherwise leave the write end of the pipe open.
 */
#define PIPEDATA "stuff"
void pipetest(int halfopen)
{
   ssize_t bc;
   int pipefd[2];
   km_hc_args_t snapshotargs;

   if (pipe(pipefd) < 0) {
      fprintf(stderr, "Couldn't create pipe, %s\n", strerror(errno));
      exit(100);
   }
   bc = write(pipefd[1], PIPEDATA, sizeof("stuff"));
   if (bc != sizeof("stuff")) {
      fprintf(stderr, "Couldn't write to pipe, bc %ld, %s\n", bc, strerror(errno));
      exit(100);
   }
   if (halfopen != 0) {
      close(pipefd[1]);
   }
   snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                 .arg2 = (uint64_t) "Snapshot pipe with data",
                                 .arg3 = 0};
   km_hcall(HC_snapshot, &snapshotargs);
   if (snapshotargs.hc_ret != 0) {
      fprintf(stderr, "snapshot pipe with data failed, error %ld\n", snapshotargs.hc_ret);
      exit(100);
   } else {
      // We should resume from the snapshot here.  So, read the pipe contents.
      char buf[32];
      ssize_t bytesread = read(pipefd[0], buf, sizeof(buf));
      if (bytesread < 0) {
         fprintf(stderr, "read from pipe failed %s\n", strerror(errno));
         exit(120);
      }
      buf[bytesread] = 0;
      fprintf(stdout, "pipedata >>>>%s<<<<\n", buf);
      if (bytesread != sizeof(PIPEDATA)) {
         fprintf(stderr, "Read %ld bytes back from pipe, expected %ld\n", bytesread, sizeof(PIPEDATA));
         exit(121);
      }
      if (strcmp(buf, PIPEDATA) != 0) {
         fprintf(stderr, "pipe data is wrong, expected %s, got %s\n", PIPEDATA, buf);
         exit(122);
      }
      close(pipefd[0]);
      if (halfopen == 0) {
         close(pipefd[1]);
      }
      exit(0);
   }
}

#define SOCKETPAIRDATA0 "chocolate chip cookie"
#define SOCKETPAIRDATA1 "krik's steak burger"
void socketpairtest(int halfopen)
{
   ssize_t bc;
   int sp[2];
   km_hc_args_t snapshotargs;

   if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
      fprintf(stderr, "Couldn't create socketpair, %s\n", strerror(errno));
      exit(100);
   }
   bc = write(sp[0], SOCKETPAIRDATA0, sizeof(SOCKETPAIRDATA0));
   if (bc != sizeof(SOCKETPAIRDATA0)) {
      fprintf(stderr, "Couldn't write to socketpair end 0, bc %ld, %s\n", bc, strerror(errno));
      exit(100);
   }
   bc = write(sp[1], SOCKETPAIRDATA1, sizeof(SOCKETPAIRDATA1));
   if (bc != sizeof(SOCKETPAIRDATA1)) {
      fprintf(stderr, "Couldn't write to socketpair end 1, bc %ld, %s\n", bc, strerror(errno));
      exit(100);
   }
   snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                 .arg2 = (uint64_t) "Snapshot socketpair with data",
                                 .arg3 = 0};
   if (halfopen != 0) {
      if (close(sp[1]) < 0) {
         // Just keep going
         fprintf(stderr, "close sockerpair fd 0 failed, %s\n", strerror(errno));
      }
   }
   km_hcall(HC_snapshot, &snapshotargs);
   if (snapshotargs.hc_ret != 0) {
      fprintf(stderr, "snapshot socketpair with data failed, error %ld\n", snapshotargs.hc_ret);
      exit(100);
   } else {
      // We should resume from the snapshot here.  So, read the socket contents from each end.
      char buf[64];
      ssize_t bytesread;

      fprintf(stdout, "Attempt to read socketpair buffered data after snapshot resume\n");

      // read from sp[0]
      bytesread = read(sp[0], buf, sizeof(buf));
      if (bytesread < 0) {
         fprintf(stderr, "read from socketpair 0 failed %s\n", strerror(errno));
         exit(120);
      }
      buf[bytesread] = 0;
      fprintf(stdout, "socketpair 0 data length %ld, >>>>%s<<<<\n", bytesread, buf);
      if (bytesread != sizeof(SOCKETPAIRDATA1)) {
         fprintf(stderr,
                 "Read %ld bytes back from socketpair 0, expected %ld\n",
                 bytesread,
                 sizeof(SOCKETPAIRDATA1));
         exit(121);
      }
      if (strcmp(buf, SOCKETPAIRDATA1) != 0) {
         fprintf(stderr, "socketpair 0 data is wrong, expected <%s>, got <%s>\n", SOCKETPAIRDATA1, buf);
         exit(122);
      }

      // read from sp[1]
      if (halfopen == 0) {
         bytesread = read(sp[1], buf, sizeof(buf));
         if (bytesread < 0) {
            fprintf(stderr, "read from socketpair 1 failed %s\n", strerror(errno));
            exit(120);
         }
         buf[bytesread] = 0;
         fprintf(stdout, "socketpair 1 data length %ld >>>>%s<<<<\n", bytesread, buf);
         if (bytesread != sizeof(SOCKETPAIRDATA0)) {
            fprintf(stderr,
                    "Read %ld bytes back from socketpair 1, expected %ld\n",
                    bytesread,
                    sizeof(SOCKETPAIRDATA0));
            exit(121);
         }
         if (strcmp(buf, SOCKETPAIRDATA0) != 0) {
            fprintf(stderr, "socketpair 1 data is wrong, expected <%s>, got <%s>\n", SOCKETPAIRDATA0, buf);
            exit(122);
         }
      }

      close(sp[0]);
      if (halfopen == 0) {
         close(sp[1]);
      }
      fprintf(stdout, "socketpair buffered data test succeeds\n");
      exit(0);
   }
}

int main(int argc, char* argv[])
{
   km_hc_args_t snapshotargs;
   int bc;
   int pipefd[2];
   pid_t pid;

   if (argc != 2) {
      usage();
      exit(100);
   }

   switch (argv[1][1]) {
      // Snapshot when there is an epoll fd with queued events?
      case 'e':;
         int epollfd = epoll_create(0);
         if (epollfd < 0) {
            fprintf(stderr, "Couldn't create epoll fd, %s\n", strerror(errno));
            exit(100);
         }
         if (pipe(pipefd) < 0) {
            fprintf(stderr, "Couldn't create pipe for epoll, %s\n", strerror(errno));
            exit(100);
         }
         struct epoll_event epollevent = {.events = EPOLLIN, .data.fd = pipefd[0]};
         if (epoll_ctl(epollfd, EPOLL_CTL_ADD, pipefd[0], &epollevent) < 0) {
            fprintf(stderr, "Couldn't add fd to epoll fd, %s", strerror(errno));
            exit(100);
         }
         bc = write(pipefd[1], "stuff", sizeof("stuff"));
         if (bc != sizeof("stuff")) {
            fprintf(stderr, "Couldn't write to epoll test pipe, bc %d, %s\n", bc, strerror(errno));
            exit(100);
         }

         snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                       .arg2 = (uint64_t) "Snapshot epoll fd with event",
                                       .arg3 = 1};
         km_hcall(HC_snapshot, &snapshotargs);
         // We expect the snapshot to fail because of the queued events on the epoll fd
         fprintf(stderr, "snapshot when epoll fd has queued events returned?\n");
         exit(100);
         break;

      // Fork then snapshot.  This should fail.
      // snapshotting pipe and socketpair buffered data is not possible if another
      // process was forked.
      case 'f': {
         int pipefd[2];
         pipe(pipefd);
         pid = fork();
         if (pid > 0) {
            sleep(1);   // allow child to write into pipe
            // we are the parent process. attempt a snapshot which should fail.
            snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                          .arg2 = (uint64_t) "Snapshot after fork",
                                          .arg3 = 1};
            km_hcall(HC_snapshot, &snapshotargs);

            // Wait for the child to terminate.
            int wstatus;
            pid_t reapedpid = waitpid(pid, &wstatus, 0);
            if (reapedpid < 0) {
               fprintf(stderr, "Waiting for child pid %d failed, %s\n", pid, strerror(errno));
               exit(112);
            }
            fprintf(stdout, "Successfully reaped child pid %d\n", reapedpid);

            // Be sure snapshot failed.
            if (snapshotargs.hc_ret == 0) {
               fprintf(stderr, "snapshot after fork() succeeded and should not\n");
               exit(111);
            }
            fprintf(stdout, "snapshot failed after fork() as expected\n");
            exit(0);
         } else if (pid < 0) {
            // Fork failed!
            fprintf(stderr, "fork failed, %s\n", strerror(errno));
            exit(110);
         } else {
            write(pipefd[1], "hello", sizeof("hello"));
            // We are the child.  Do nothing just exit.
            exit(0);
         }
         break;
      }
      // Snapshot when a pipe has queued data
      case 'p':;
         pipetest(0);
         break;

      // Half open pipe with buffered data
      case 'h':
         pipetest(1);
         break;

      // Snapshot with a connected network connection
      case 'c':;
         int listenfd;
         int connectfd;
         int acceptfd;
         unsigned short listenport;
         char* port = getenv(SOCKET_PORT);
         if (port == NULL) {
            fprintf(stderr,
                    "The connection test needs a network defined in the env var %s\n",
                    SOCKET_PORT);
            exit(100);
         }
         listenport = atoi(port);
         listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
         if (listenfd < 0) {
            fprintf(stderr, "Couldn't create socket for listening, %s\n", strerror(errno));
            exit(100);
         }
         int opt = 1;
         if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            fprintf(stderr, "Couldn't set SO_REUSEADDR on listen socket, %s", strerror(errno));
            exit(100);
         }
         struct sockaddr sa;
         struct sockaddr_in* sap = (struct sockaddr_in*)&sa;
         sap->sin_family = AF_INET;
         sap->sin_port = htons(listenport);
         sap->sin_addr.s_addr = inet_addr("127.0.0.1");
         int socklen = sizeof(*sap);
         int rc = bind(listenfd, &sa, socklen);
         if (rc < 0) {
            fprintf(stderr, "Couldn't bind address to socket, %s\n", strerror(errno));
            exit(100);
         }
         if (listen(listenfd, 1) < 0) {
            fprintf(stderr, "Couldn't listen on socket, %s\n", strerror(errno));
            exit(100);
         }

         // Do a non-blocking connect
         connectfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
         if (connectfd < 0) {
            fprintf(stderr, "Couldn't create socket for connecting, %s\n", strerror(errno));
            exit(100);
         }
         // set non-blocking
         if (fcntl(connectfd, F_SETFL, O_NONBLOCK) < 0) {
            fprintf(stderr, "Couldn't set connect socket to non-blocking mode, %s\n", strerror(errno));
            exit(100);
         }
         struct sockaddr_in connect_sa;
         int connect_socklen = sizeof(connect_sa);
         connect_sa.sin_family = AF_INET;
         connect_sa.sin_port = htons(listenport);
         connect_sa.sin_addr.s_addr = inet_addr("127.0.0.1");
         if (connect(connectfd, &connect_sa, connect_socklen) < 0) {
            if (errno != EINPROGRESS) {
               fprintf(stderr, "Couldn't connect, %s\n", strerror(errno));
               exit(100);
            }
         }

         // Now do the accept
         struct sockaddr_in accepted_sa;
         socklen_t accepted_socklen;
         acceptfd = accept(listenfd, &accepted_sa, &accepted_socklen);
         if (acceptfd < 0) {
            fprintf(stderr, "accept failed, %s\n", strerror(errno));
            exit(100);
         }
         fprintf(stdout, "accept successful, fd %d\n", acceptfd);

         // Finally we can take the snapshot.
         snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                       .arg2 = (uint64_t) "Snapshot connected socket",
                                       .arg3 = 1};
         km_hcall(HC_snapshot, &snapshotargs);
         // We expect the snapshot to fail because of the queued socket data.
         fprintf(stderr, "snapshot returned when socket was connected?\n");
         exit(100);
         break;

      // Snapshot when a socketpair has queued data
      case 's':
         socketpairtest(0);
         break;

      // Half open socketpair with buffered data
      case 'm':
         socketpairtest(1);
         break;

      // Snapshot when there is an active setitimer() timer.  We should try all of the internal timer types.
      case 't':;
         struct itimerval val = {{0, 0}, {10, 0}};   // fire once after 10 seconds (azure will stall
                                                     // us this long for sure :-()
         if (setitimer(ITIMER_REAL, &val, NULL) < 0) {
            fprintf(stderr, "setitimer() failed, %s\n", strerror(errno));
            exit(100);
         }
         snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                       .arg2 = (uint64_t) "Snapshot socketpair with data",
                                       .arg3 = 1};
         km_hcall(HC_snapshot, &snapshotargs);
         // We expect the snapshot to fail because of the queued socket data.
         fprintf(stderr, "snapshot when interval timer is active returned %lu\n", snapshotargs.hc_ret);
         exit(100);
         break;

      // Snapshot when there is an active create_timer() timer
      case 'z':
         break;

      default:
         usage();
         exit(100);
         break;
   }

   // We shouldn't get here.  snapshot failure causes km to exit.
   fprintf(stderr, "Unprocessed command line option %s\n", argv[1]);
   return 100;
}
