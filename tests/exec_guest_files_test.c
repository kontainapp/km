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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/*
 * This test opens all of the types of files that km snapshot does special things with
 * then does an exec to itself.  We are trying to verify that the special information
 * that snapshot needs makes to the km in the exec target program.
 * Since the test program can't verify that the open file
 * state in the exec target is what is expected, we depending on tracing being enabled
 * and then examine the trace to verify that km file state in the exec target is correct.
 * The needed traces are produced by the km function km_exec_fdtrace()
 */

void usage(void)
{
   fprintf(stderr, "usage: exec_guest_files_test [ exec_to_target | be_the_target fd .. ]\n");
}

int main(int argc, char* argv[])
{
   int plainfilefd;
   int sockfd;
   int sockfd2;
   int socketpairfd[2] = {123, 456};
   int pipefd[2] = {789, 012};
   int epollfd;
   int rc;

   if (argc < 2) {
      usage();
      return 1;
   }

   if (strcmp(argv[1], "exec_to_target") == 0) {
      fprintf(stderr, "processing exec_to_target\n");

      // Open a plain file
      struct timespec ts;
      char plainfilename[128];
      clock_gettime(CLOCK_MONOTONIC, &ts);
      snprintf(plainfilename,
               sizeof(plainfilename),
               "/tmp/exec_guest_files_test_%ld.%ld",
               ts.tv_sec,
               ts.tv_nsec);
      plainfilefd = open(plainfilename, O_CREAT | O_EXCL, 0666);
      if (plainfilefd < 0) {
         fprintf(stderr, "open %s failed, %s\n", plainfilename, strerror(errno));
         return 1;
      }

      // open a socket and bind
      char* portstring = getenv("SOCKET_PORT");
      if (portstring == NULL) {
         fprintf(stderr, "SOCKET_PORT=n must be defined in the enviroment\n");
         return 1;
      }
      uint16_t port = atoi(portstring);
      struct sockaddr_in sockaddr;
      sockaddr.sin_family = AF_INET;
      sockaddr.sin_port = htons(port);
      inet_aton("0.0.0.0", &sockaddr.sin_addr);
      sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sockfd < 0) {
         fprintf(stderr, "socket create failed, %s\n", strerror(errno));
         return 1;
      }
      rc = bind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
      if (rc < 0) {
         fprintf(stderr, "bind to port %d failed, %s\n", port, strerror(errno));
         return 1;
      }

      // open a socket and bind and listen
      sockaddr.sin_port = htons(port + 1);
      inet_aton("0.0.0.0", &sockaddr.sin_addr);
      sockfd2 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sockfd2 < 0) {
         fprintf(stderr, "socket create failed, %s\n", strerror(errno));
         return 1;
      }
      rc = bind(sockfd2, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
      if (rc < 0) {
         fprintf(stderr, "bind to port %d failed, %s\n", port + 1, strerror(errno));
         return 1;
      }
      rc = listen(sockfd2, 5);
      if (rc < 0) {
         fprintf(stderr, "listen failed, %s\n", strerror(errno));
         return 1;
      }

      // open a socketpair
      rc = socketpair(AF_UNIX, SOCK_STREAM, 0, socketpairfd);
      if (rc < 0) {
         fprintf(stderr, "socketpair create failed, %s\n", strerror(errno));
         return 1;
      }

      // create a pipe
      rc = pipe(pipefd);
      if (rc < 0) {
         fprintf(stderr, "pipe create failed, %s\n", strerror(errno));
         return 1;
      }

      // create an epoll fd and add 3 events to it
      struct epoll_event event;
      epollfd = epoll_create(22);
      if (epollfd < 0) {
         fprintf(stderr, "epoll_create failed, %s\n", strerror(errno));
         return 1;
      }
      event.events = EPOLLIN;
      event.data.fd = sockfd;
      rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event);
      event.events = EPOLLOUT;
      event.data.fd = plainfilefd;
      rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, plainfilefd, &event);
      event.events = EPOLLOUT;
      event.data.fd = socketpairfd[0];
      rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, socketpairfd[0], &event);

      fprintf(stderr,
              "plainfilefd %d, sockfd %d, sockfd2 %d, socketpairfd[0] %d, socketpairfd[1] %d, "
              "pipefd[0] %d, pipefd[1] %d, epollfd %d\n",
              plainfilefd,
              sockfd,
              sockfd2,
              socketpairfd[0],
              socketpairfd[1],
              pipefd[0],
              pipefd[1],
              epollfd);

      // exec to this program again with the fd's we opened as arguments
      char me[PATH_MAX];
      char* argv[20];
      char* env[1] = {NULL};
      rc = readlink("/proc/self/exe", me, sizeof(me));
      if (rc < 0) {
         fprintf(stderr, "Can't readlink /proc/self/me?, %s\n", strerror(errno));
         return 1;
      }
      me[rc] = 0;
      argv[0] = me;
      argv[1] = "be_the_target";
      argv[2] = plainfilename;
      asprintf(&argv[3], "%d", plainfilefd);
      asprintf(&argv[4], "%d", sockfd);
      asprintf(&argv[5], "%d", sockfd2);
      asprintf(&argv[6], "%d", socketpairfd[0]);
      asprintf(&argv[7], "%d", socketpairfd[1]);
      asprintf(&argv[8], "%d", pipefd[0]);
      asprintf(&argv[9], "%d", pipefd[1]);
      asprintf(&argv[10], "%d", epollfd);
      argv[11] = NULL;
      rc = execve(me, argv, env);
      // If execve returned, something failed.
      fprintf(stderr, "execve to %s failed, %s\n", me, strerror(errno));
      return 1;
   } else if (strcmp(argv[1], "be_the_target") == 0) {
      fprintf(stderr, "processing be_the_target\n");
      plainfilefd = atoi(argv[3]);
      sockfd = atoi(argv[4]);
      sockfd2 = atoi(argv[5]);
      socketpairfd[0] = atoi(argv[6]);
      socketpairfd[1] = atoi(argv[7]);
      pipefd[0] = atoi(argv[8]);
      pipefd[1] = atoi(argv[9]);
      epollfd = atoi(argv[10]);

      fprintf(stderr,
              "plainfilefd %d, sockfd %d, sockfd2 %d, socketpairfd[0] %d, socketpairfd[1] %d, "
              "pipefd[0] %d, pipefd[1] %d, epollfd %d\n",
              plainfilefd,
              sockfd,
              sockfd2,
              socketpairfd[0],
              socketpairfd[1],
              pipefd[0],
              pipefd[1],
              epollfd);

      rc = close(plainfilefd);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close plainfilefd, %s\n", strerror(errno));
      }
      rc = close(sockfd);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close sockfd %d, %s\n", sockfd, strerror(errno));
      }
      rc = close(sockfd2);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close sockfd2 %d, %s\n", sockfd2, strerror(errno));
      }
      rc = close(socketpairfd[0]);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close socketpairfd[0], %s\n", strerror(errno));
      }
      rc = close(socketpairfd[1]);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close socketpairfd[1], %s\n", strerror(errno));
      }
      rc = close(pipefd[0]);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close pipefd[0], %s\n", strerror(errno));
      }
      rc = close(pipefd[1]);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close pipefd[1], %s\n", strerror(errno));
      }
      rc = close(epollfd);
      if (rc < 0) {
         fprintf(stderr, "Couldn't close epollfd, %s\n", strerror(errno));
      }

      rc = unlink(argv[2]);
      if (rc < 0) {
         fprintf(stderr, "Couldn't unlink test file: %s, %s\n", argv[2], strerror(errno));
      }
   } else {
      fprintf(stderr, "Don't know how to do: %s\n", argv[1]);
      return 1;
   }
   return 0;
}
