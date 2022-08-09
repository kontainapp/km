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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static in_port_t PORT = 8002;

void tcp_close(int sd)
{
   shutdown(sd, SHUT_RDWR); /* no more receptions */
   close(sd);
}

struct sockaddr_in sa_serv;

int tcp_listen(void)
{
   int listen_sd;
   int optval = 1;

   listen_sd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_sd < 0) {
      puts("listen socket\n");
      exit(1);
   }

   sa_serv.sin_family = AF_INET;
   sa_serv.sin_addr.s_addr = INADDR_ANY;
   sa_serv.sin_port = htons(PORT);

   setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

   if (bind(listen_sd, (struct sockaddr*)&sa_serv, sizeof(sa_serv)) < 0) {
      printf("bind, port %d failed, %s\n", PORT, strerror(errno));
      close(listen_sd);
      exit(1);
   }

   if (listen(listen_sd, 1024) < 0) {
      printf("listen failed, %s\n", strerror(errno));
      close(listen_sd);
      exit(1);
   }
   return listen_sd;
}

int tcp_accept(int listen_sd)
{
   int sd;
   struct sockaddr_in sa_cli;
   socklen_t client_len = sizeof(sa_cli);
   char topbuf[512];

   sd = accept(listen_sd, (struct sockaddr*)&sa_cli, &client_len);
   if (sd < 0) {
      printf("accept failed, %s\n", strerror(errno));
   } else {
      printf("- connection from %s, port %d, addrlen %d\n",
             inet_ntop(AF_INET, &sa_cli.sin_addr, topbuf, sizeof(topbuf)),
             ntohs(sa_cli.sin_port),
             client_len);
   }

   return sd;
}

char wbuf[] = "HTTP/1.1 200 OK\r\n"
              "Date: Mon, 10 Dec 2019 22:23:47 GMT\r\n"
              "Server: Apache\r\n"
              "Last-Modified: Sat, 01 Nov 2014 04:18:40 GMT\r\n"
              "Content-Length: 110\r\n"
              "Connection: close\r\n"
              "Content-Type: text/html\r\n"
              "\r\n"
              "<!doctype html>\r\n"
              "<html>\r\n"
              "<head>\r\n"
              "	<title>Hello, world!</title>\r\n"
              "</head>\r\n"
              "<body>\r\n"
              "	<h1>I'm here</h1>\r\n"
              "</body>\r\n"
              "</html>\r\n";

int main(int argc, char const* argv[])
{
   char buf[4096];
   int listen_sd, sd;
   ssize_t ret;
   int keep_running;

   if (argc == 2) {
      PORT = atoi(argv[1]);
   }
   keep_running = (getenv("KEEP_RUNNING") != NULL);

   listen_sd = tcp_listen();
   printf("Listening on port %d\n", PORT);
   do {
      sd = tcp_accept(listen_sd);
      if (sd < 0) {
         tcp_close(listen_sd);
         exit(1);
      }
      ret = read(sd, buf, sizeof(buf) - 1);
      if (ret >= 0) {
         buf[ret] = 0;
      } else {
         printf("read failed, %s\n", strerror(errno));
         buf[0] = 0;
      }
      ret = write(sd, wbuf, sizeof(wbuf));
      if (ret < 0) {
         printf("write failed, %s\n", strerror(errno));
      }
      tcp_close(sd);
      if (strstr(buf, "stop") != NULL) {
         break;
      }
   } while (keep_running != 0);
   tcp_close(listen_sd);
   exit(0);
}
