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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/*
 * A small test that creates several listening sockets and then blocks
 * in select for connection requests to any of the listening sockets.
 * It receives up to 10 kilobytes of data and sends a goofy response
 * and then closes the connection.
 * This test is currently not multi-threaded.
 * The intended purpose of this test is to exercise the light snap listen
 * code's ability to handle listening on multiple sockets and rehydrating
 * a shrunken snapshot on connect, then after a timeout the snapshot is shrunken again
 * for the cycle to repeat.
 * We want to verify that the listening sockets persist across rehydrate/shrink
 * cycles.
 *
 * Usage:
 *   multilisten_test.km number_of_listening_sockets starting_listen_port
 */

#define MAXLISTENFD 64
char* progname;

void usage(void)
{
   fprintf(stderr, "usage: %s number_of_listening_sockets starting_listen_port\n", progname);
}

int main(int argc, char* argv[])
{
   int listenfd[MAXLISTENFD];

   progname = argv[0];
   if (argc != 3) {
      usage();
      return 1;
   }
   int number_listening = atoi(argv[1]);
   int starting_port = atoi(argv[2]);
   if (number_listening > MAXLISTENFD) {
      fprintf(stderr, "number of listening sockets can't exceed %d\n", MAXLISTENFD);
      return 1;
   }
   fprintf(stdout,
           "first listening port %d, number of ports to listen on %d\n",
           starting_port,
           number_listening);

   // Start listening on ports
   int maxfd = -1;
   for (int i = 0; i < number_listening; i++) {
      listenfd[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (listenfd[i] < 0) {
         fprintf(stderr, "Couldn't create socket, %s\n", strerror(errno));
         return 1;
      }
      int flag = 1;
      if (setsockopt(listenfd[i], SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) {
         fprintf(stderr, "setsockopt( SO_REUSEADDR ) failed, %s\n", strerror(errno));
         return 1;
      }
      struct sockaddr_in sock;
      sock.sin_family = AF_INET;
      sock.sin_port = htons(starting_port + i);
      sock.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(listenfd[i], (struct sockaddr*)&sock, sizeof(sock)) < 0) {
         fprintf(stderr, "Couldn't bind port %d, %s\n", starting_port + 1, strerror(errno));
         return 1;
      }
      if (listen(listenfd[i], 1) < 0) {
         fprintf(stderr, "Couldn't list on port %d, %s\n", starting_port + 1, strerror(errno));
         return 1;
      }
      if (listenfd[i] > maxfd) {
         maxfd = listenfd[i];
      }
   }

   // Setup select mask to listen for connections
   fd_set readfds;
   FD_ZERO(&readfds);
   for (int i = 0; i < number_listening; i++) {
      FD_SET(listenfd[i], &readfds);
   }

   // Wait for new connections, read stuff, write stuff, close connection, repeat.
   for (;;) {
      fd_set connected;
      connected = readfds;
      int numfds = select(maxfd + 1, &connected, NULL, NULL, NULL);
      if (numfds < 0) {
         fprintf(stderr, "select failed, %s\n", strerror(errno));
         return 1;
      }
      if (numfds == 0) {
         continue;
      }
      // find the fd to do the accept on
      struct sockaddr sockaddr;
      socklen_t socklen;
      int newconn = -1;
      for (int i = 0; i < number_listening; i++) {
         if (FD_ISSET(listenfd[i], &connected)) {
            socklen = sizeof(sockaddr);
            if ((newconn = accept(listenfd[i], &sockaddr, &socklen)) < 0) {
               fprintf(stderr, "accept on fd %d failed, %s\n", listenfd[i], strerror(errno));
               return 1;
            }
            break;
         }
      }
      struct sockaddr sockname;
      socklen = sizeof(sockname);
      if (getsockname(newconn, &sockname, &socklen) < 0) {
         fprintf(stderr, "getsockname failed, %s\n", strerror(errno));
         return 1;
      }
      char string1[64];
      char string2[64];
      inet_ntop(sockaddr.sa_family, &((struct sockaddr_in*)&sockaddr)->sin_addr, string1, sizeof(string1));
      inet_ntop(sockname.sa_family, &((struct sockaddr_in*)&sockname)->sin_addr, string2, sizeof(string2));
      fprintf(stdout,
              "Got connection from %s port %d on %s, port %d\n",
              string1,
              ntohs(((struct sockaddr_in*)&sockaddr)->sin_port),
              string2,
              ntohs(((struct sockaddr_in*)&sockname)->sin_port));

      // Read request, send response, close
      // We don't necessarily receive all of what has been sent and that is intentional.
      // We don't really care about the message content.
      if (newconn >= 0) {
         uint8_t buffer[10 * 1024];
         ssize_t bytesread = recv(newconn, buffer, sizeof(buffer), 0);
         if (bytesread < 0) {
            fprintf(stderr, "recv() failed, %s\n", strerror(errno));
            close(newconn);
            continue;
         }
         char message[64];
         snprintf(message,
                  sizeof(message),
                  "goofy message from port %d\n",
                  ntohs(((struct sockaddr_in*)(&sockname))->sin_port));
         ssize_t byteswritten = send(newconn, message, sizeof(message), 0);
         if (byteswritten < 0) {
            fprintf(stderr, "send() failed, %s\n", strerror(errno));
            close(newconn);
            continue;
         }
         close(newconn);
         if (strstr((char*)buffer, "terminate") != NULL) {
            fprintf(stdout, "Requested to terminate\n");
            break;
         }
      } else {
         fprintf(stderr, "select() returned but no fd's fired?\n");
      }
   }

   for (int i = 0; i < number_listening; i++) {
      close(listenfd[i]);
   }

   return 0;
}
