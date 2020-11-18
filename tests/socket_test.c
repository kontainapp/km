/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Tests for socket oriented system calls
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "greatest/greatest.h"
#include "syscall.h"

TEST test_socket_create()
{
   int fd;
   int rc;
   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   ASSERT_NOT_EQ(-1, fd);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   fd = socket(AF_INET, SOCK_STREAM, 0);
   ASSERT_NOT_EQ(-1, fd);
   ASSERT_EQ(0, close(fd));

   fd = socket(AF_INET, SOCK_DGRAM, 0);
   ASSERT_NOT_EQ(-1, fd);
   rc = close(fd);
   ASSERT_EQ(0, rc);
   PASS();
}

TEST test_sockopt()
{
   int fd;
   int rc;
   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   ASSERT_NOT_EQ(-1, fd);
   int opt;
   socklen_t optlen = sizeof(opt);
   rc = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt, &optlen);
   ASSERT_EQ(0, rc);
   rc = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)-1, &optlen);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt, (void*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = close(fd);
   ASSERT_EQ(0, rc);

   PASS();
}

int TEST_PORT;
void* tcp_server_main(void* arg)
{
   static int rc = 0;
   int fd = *(int*)arg;

   if (listen(fd, 10) < 0) {
      rc = errno;
      return &rc;
   }

   struct sockaddr caddr;
   socklen_t caddr_len;
   int newfd = accept4(fd, &caddr, &caddr_len, SOCK_CLOEXEC);
   if (newfd < 0) {
      rc = errno;
      return &rc;
   }

   struct pollfd pfd = {.fd = newfd, .events = POLLIN | POLLERR};
   if (poll(&pfd, 1, -1) < 1) {
      rc = errno;
      return &rc;
   }
   if ((pfd.events & POLLERR) == 0) {
      rc = errno;
      return &rc;
   }
   close(newfd);
   return &rc;
}

TEST test_tcp()
{
   // Server socket
   int sfd;
   int rc;
   sfd = socket(AF_INET, SOCK_STREAM, 0);
   ASSERT_NOT_EQ(-1, sfd);
   struct sockaddr_in saddr;
   saddr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
   saddr.sin_port = TEST_PORT;
   rc = bind(sfd, (struct sockaddr*)&saddr, sizeof(saddr));
   ASSERT_EQ(0, rc);

   pthread_t thr;
   rc = pthread_create(&thr, NULL, tcp_server_main, &sfd);
   ASSERT_EQ(0, rc);

   // Client Socket. Connect then close.
   int cfd;
   cfd = socket(AF_INET, SOCK_STREAM, 0);
   ASSERT_NOT_EQ(-1, cfd);

   struct sockaddr_in caddr = {};
   caddr.sin_family = AF_INET;
   caddr.sin_port = TEST_PORT;
   inet_pton(AF_INET, "127.0.0.1", &caddr.sin_addr);

   sleep(1);
   rc = connect(cfd, (struct sockaddr*)&caddr, sizeof(caddr));
   ASSERT_EQ(0, rc);

   struct sockaddr_storage addr;
   socklen_t addrlen = sizeof(addr);
   rc = getpeername(cfd, (struct sockaddr*)&addr, &addrlen);
   ASSERT_EQ(0, rc);

   rc = close(cfd);
   ASSERT_EQ(0, rc);

   // close of client should end thread
   void* rvalp = NULL;
   rc = pthread_join(thr, &rvalp);
   ASSERT_EQ(0, rc);
   ASSERT_NOT_EQ(NULL, rvalp);
   ASSERT_EQ(0, *(int*)rvalp);

   rc = close(sfd);
   ASSERT_EQ(0, rc);

   PASS();
}

int TEST_UDP_PORT1;
int TEST_UDP_PORT2;

TEST test_udp()
{
   int rc;
   int epfd = epoll_create(0);
   ASSERT_NOT_EQ(-1, epfd);

   int fd1;
   fd1 = socket(AF_INET, SOCK_DGRAM, 0);
   ASSERT_NOT_EQ(-1, fd1);
   struct sockaddr_in addr1;
   addr1.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &addr1.sin_addr);
   addr1.sin_port = TEST_UDP_PORT1;
   rc = bind(fd1, (struct sockaddr*)&addr1, sizeof(addr1));
   ASSERT_EQ(0, rc);

   int fd2;
   fd2 = socket(AF_INET, SOCK_DGRAM, 0);
   ASSERT_NOT_EQ(-1, fd2);
   struct sockaddr_in addr2;
   addr2.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &addr2.sin_addr);
   addr2.sin_port = TEST_UDP_PORT2;
   rc = bind(fd2, (struct sockaddr*)&addr2, sizeof(addr2));
   ASSERT_EQ(0, rc);

   struct epoll_event event = {.events = EPOLLIN | EPOLLERR, .data.fd = fd2};
   rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fd2, &event);
   ASSERT_EQ(0, rc);

   char* msg = "Hello from KM";
   int sent = sendto(fd1, msg, strlen(msg) + 1, 0, (struct sockaddr*)&addr2, sizeof(addr2));
   ASSERT_EQ(strlen(msg) + 1, sent);

   struct epoll_event revents[5];
   rc = epoll_wait(epfd, revents, 5, -1);
   ASSERT_EQ(1, rc);

   char buf[1024];
   struct sockaddr raddr;
   socklen_t raddr_len = sizeof(raddr);
   int rcvd = recvfrom(fd2, buf, sizeof(buf), 0, &raddr, &raddr_len);
   ASSERT_EQ(sent, rcvd);
   ASSERT_EQ(0, strcmp(msg, buf));

   rc = close(fd1);
   ASSERT_EQ(0, rc);
   rc = close(fd2);
   ASSERT_EQ(0, rc);
   rc = close(epfd);
   ASSERT_EQ(0, rc);

   PASS();
}

/*
 * Test socket oriented syscalls with bad fd's.
 */
int badfd[] = {-1, 1000000, 2000, 0};
TEST test_bad_fd()
{
   int rc;
   // getsockopt
   for (int i = 0; badfd[i] != 0; i++) {
      int opt;
      socklen_t optlen = sizeof(opt);
      rc = getsockopt(badfd[i], 0, 0, &opt, &optlen);
      ASSERT_EQ_FMT(-1, rc, "%d");
      ASSERT_EQ(EBADF, errno);
   }
   // setsockopt
   for (int i = 0; badfd[i] != 0; i++) {
      int opt;
      socklen_t optlen = sizeof(opt);
      rc = setsockopt(badfd[i], 0, 0, &opt, optlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // getpeername
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      rc = getpeername(badfd[i], &addr, &addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // bind
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      rc = bind(badfd[i], &addr, addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // listen
   for (int i = 0; badfd[i] != 0; i++) {
      rc = listen(badfd[i], 10);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // accept
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      rc = accept(badfd[i], &addr, &addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // accept4
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      rc = accept4(badfd[i], &addr, &addrlen, O_CLOEXEC);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // accept4
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      rc = connect(badfd[i], &addr, addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // sendto
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      char buf[1024];
      rc = sendto(badfd[i], buf, sizeof(buf), 0, &addr, addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // recvfrom
   for (int i = 0; badfd[i] != 0; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      char buf[1024];
      rc = recvfrom(badfd[i], buf, sizeof(buf), 0, &addr, &addrlen);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // poll
   for (int i = 0; badfd[i] != 0; i++) {
      struct pollfd pfd;
      pfd.fd = badfd[i];
      pfd.events = POLLIN | POLLERR;
      rc = poll(&pfd, 1, 0);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // epoll_ctl
   for (int i = 0; badfd[i] != 0; i++) {
      struct epoll_event event;
      rc = epoll_ctl(badfd[i], 0, 0, &event);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
      rc = epoll_ctl(0, 0, badfd[i], &event);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
      rc = epoll_ctl(badfd[i], 0, badfd[i], &event);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   // epoll_wait
   for (int i = 0; badfd[i] != 0; i++) {
      struct epoll_event revent;
      rc = epoll_wait(badfd[i], &revent, 1, -1);
      ASSERT_EQ(-1, rc);
      ASSERT_EQ(EBADF, errno);
   }
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   // Get some port numbers that won't interfere with other running instances of this test.
   char* port = getenv("SOCKET_PORT");
   uint16_t baseport = 6565;
   if (port != NULL) {
      baseport = atoi(port);
   }
   TEST_PORT = htons(baseport);
   baseport++;
   TEST_UDP_PORT1 = htons(baseport);
   baseport++;
   TEST_UDP_PORT2 = htons(baseport);

   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_socket_create);
   RUN_TEST(test_sockopt);
   RUN_TEST(test_udp);
   RUN_TEST(test_tcp);
   RUN_TEST(test_bad_fd);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
