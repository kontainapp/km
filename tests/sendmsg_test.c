/*
 * Copyright 2023 Kontain Inc
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

#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "greatest/greatest.h"

struct sockaddr sendaddr;
socklen_t sendaddrlen;
struct sockaddr recvaddr;
socklen_t recvaddrlen;

TEST test_sendmsg()
{
   int sendfd;
   int recvfd;

   ASSERT((sendfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
   ASSERT(bind(sendfd, &sendaddr, sendaddrlen) == 0);
   ASSERT((recvfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
   ASSERT(bind(recvfd, &recvaddr, recvaddrlen) == 0);

   char* msg = "Hello World";
   struct iovec iov = {.iov_base = msg, .iov_len = strlen(msg)};
   struct msghdr msghdr = {.msg_name = &recvaddr,
                           .msg_namelen = recvaddrlen,
                           .msg_iov = &iov,
                           .msg_iovlen = 1};
   ASSERT(sendmsg(sendfd, &msghdr, 0) == iov.iov_len);

   char buffer[1024];
   memset(buffer, 0, sizeof(buffer));
   struct iovec riov = {.iov_base = buffer, .iov_len = sizeof(buffer)};
   struct msghdr rmsghdr = {.msg_iov = &riov, .msg_iovlen = 1};
   ASSERT(recvmsg(recvfd, &rmsghdr, 0) == iov.iov_len);
   ASSERT(strcmp(buffer, "Hello World") == 0);

   close(recvfd);
   close(sendfd);
   PASS();
}

TEST test_sendmmsg()
{
   int sendfd;
   int recvfd;

   ASSERT((sendfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
   ASSERT(bind(sendfd, &sendaddr, sendaddrlen) == 0);
   ASSERT((recvfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
   ASSERT(bind(recvfd, &recvaddr, recvaddrlen) == 0);

   char* msg = "Hello World";
   struct iovec iov = {.iov_base = msg, .iov_len = strlen(msg)};
   char* msg2 = "Hello Moon";
   struct iovec iov2 = {.iov_base = msg2, .iov_len = strlen(msg2)};
   struct mmsghdr mmsghdr[2] =
       {{.msg_hdr = {.msg_name = &recvaddr, .msg_namelen = recvaddrlen, .msg_iov = &iov, .msg_iovlen = 1},
         .msg_len = 0},
        {.msg_hdr = {.msg_name = &recvaddr, .msg_namelen = recvaddrlen, .msg_iov = &iov2, .msg_iovlen = 1},
         .msg_len = 0}};

   // Send 2 messages to reciver
   ASSERT(sendmmsg(sendfd, mmsghdr, 2, 0) == 2);

   char buffer[1024];
   memset(buffer, 0, sizeof(buffer));
   struct iovec riov = {.iov_base = buffer, .iov_len = sizeof(buffer)};
   char buffer2[1024];
   memset(buffer2, 0, sizeof(buffer2));
   struct iovec riov2 = {.iov_base = buffer2, .iov_len = sizeof(buffer2)};
   struct mmsghdr rmmsghdr[2] = {{.msg_hdr = {.msg_iov = &riov, .msg_iovlen = 1}},
                                 {.msg_hdr = {.msg_iov = &riov2, .msg_iovlen = 1}}};

   ASSERT(recvmmsg(recvfd, rmmsghdr, 2, 0, NULL) == 2);
   ASSERT(rmmsghdr[0].msg_len == strlen(msg));
   ASSERT(memcmp(rmmsghdr[0].msg_hdr.msg_iov->iov_base, msg, strlen(msg)) == 0);
   ASSERT(rmmsghdr[1].msg_len == strlen(msg2));
   ASSERT(memcmp(rmmsghdr[1].msg_hdr.msg_iov->iov_base, msg2, strlen(msg2)) == 0);

   close(recvfd);
   close(sendfd);
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   struct addrinfo* ar;
   int err;

   if ((err = getaddrinfo("127.0.0.1", "7777", NULL, &ar)) != 0) {
      fprintf(stderr, "ERROR: getaddrinfo 127.0.0.1:7777 failed:%s\n", gai_strerror(err));
      if (err == EAI_SYSTEM) {
         perror("getaddrinfo");
      }
      return 1;
   }
   memcpy(&sendaddr, ar[0].ai_addr, ar[0].ai_addrlen);
   sendaddrlen = ar[0].ai_addrlen;
   freeaddrinfo(ar);

   if ((err = getaddrinfo("127.0.0.1", "7778", NULL, &ar)) != 0) {
      fprintf(stderr, "ERROR: getaddrinfo 127.0.0.1:7778 failed:%s\n", gai_strerror(err));
      if (err == EAI_SYSTEM) {
         perror("getaddrinfo");
      }
      return 1;
   }
   memcpy(&recvaddr, ar[0].ai_addr, ar[0].ai_addrlen);
   recvaddrlen = ar[0].ai_addrlen;
   freeaddrinfo(ar);

   GREATEST_MAIN_BEGIN();
   RUN_TEST(test_sendmsg);
   RUN_TEST(test_sendmmsg);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
