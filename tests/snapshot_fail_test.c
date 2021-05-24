/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
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
#include <arpa/inet.h>
#include <netinet/in.h>

#include "km_hcalls.h"

// Some tests need a network port which is to be provided via the environment variable named here.
char* SOCKET_PORT = "SOCKET_PORT";

/*
 * A simple test to verify that snapshots fail if the following are found in the
 * payload being snapshotted.
 * - epoll fd's with events
 * - pipes with queued data
 * - socketpair's with queued data
 * - sockets that are in a connected state.
 * - interval timers that are running (setitimer() and timer_create())
 * We can try each of these things with different command line options.
 * We verify success by this program exiting with an error status and an error message.
 * Command line flags:
 *  -e - eventfd
 *  -p - pipe
 *  -c - connected socket
 *  -s - socketpair
 *  -t - active interval timers (setitimer() type timers)
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
   fprintf(stderr, "       -p = pipe with queued data\n");
   fprintf(stderr, "       -c = connected socket\n");
   fprintf(stderr, "       -s = socketpair with queued data\n");
   fprintf(stderr, "       -t = active setitimer interval timer\n");
   fprintf(stderr, "       -z = active timer_create interval timer (km does not support these yet)\n");
}

int main(int argc, char* argv[])
{
   km_hc_args_t snapshotargs;
   int bc;
   int pipefd[2];

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

      // Snapshot when a pipe has queued data
      case 'p':;
         if (pipe(pipefd) < 0) {
            fprintf(stderr, "Couldn't create pipe, %s\n", strerror(errno));
            exit(100);
         }
         bc = write(pipefd[1], "stuff", sizeof("stuff"));
         if (bc != sizeof("stuff")) {
            fprintf(stderr, "Couldn't write to pipe, bc %d, %s\n", bc, strerror(errno));
            exit(100);
         }
         snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                       .arg2 = (uint64_t) "Snapshot pipe with data",
                                       .arg3 = 1};
         km_hcall(HC_snapshot, &snapshotargs);
         // We expect the snapshot to fail because of the queued pipe data.
         fprintf(stderr, "snapshot when pipe has queued data returned?\n");
         exit(100);
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
      case 's':;
         int sp[2];
         if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
            fprintf(stderr, "Couldn't create socketpair, %s\n", strerror(errno));
            exit(100);
         }
         bc = write(sp[1], "stuff", sizeof("stuff"));
         if (bc != sizeof("stuff")) {
            fprintf(stderr, "Couldn't write to socketpair, bc %d, %s\n", bc, strerror(errno));
            exit(100);
         }
         snapshotargs = (km_hc_args_t){.arg1 = (uint64_t) "snaptest_label",
                                       .arg2 = (uint64_t) "Snapshot socketpair with data",
                                       .arg3 = 1};
         km_hcall(HC_snapshot, &snapshotargs);
         // We expect the snapshot to fail because of the queued socket data.
         fprintf(stderr, "snapshot when socketpair has queued data returned?\n");
         exit(100);
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
