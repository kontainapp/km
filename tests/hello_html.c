/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define __bswap_16(x) ((__uint16_t)((((x) >> 8) & 0xff) | (((x)&0xff) << 8)))

static const in_port_t PORT = 8002;

// void tcp_close(int sd)
// {
//    shutdown(sd, SHUT_RDWR); /* no more receptions */
//    close(sd);
// }

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

   if (bind(listen_sd, (struct sockaddr *)&sa_serv, sizeof(sa_serv)) < 0) {
      puts("bind\n");
      exit(1);
   }

   if (listen(listen_sd, 1024) < 0) {
      puts("listen\n");
      exit(1);
   }
   return listen_sd;
}

int tcp_accept(int listen_sd)
{
   int sd;
   socklen_t client_len;
   struct sockaddr_in sa_cli;
   //    char topbuf[512];

   sd = accept(listen_sd, (struct sockaddr *)&sa_cli, &client_len);

   //    printf("- connection from %s, port %d\n",
   //           inet_ntop(AF_INET, &sa_cli.sin_addr, topbuf, sizeof(topbuf)),
   //           ntohs(sa_cli.sin_port));

   return sd;
}

char wbuf[] = "HTTP/1.1 200 OK\r\n"
              "Date: Mon, 10 Dec 2018 22:23:47 GMT\r\n"
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

int main(int argc, char const *argv[])
{
   char buf[4096];
   int listen_sd, sd;
   size_t ret;

   listen_sd = tcp_listen();
   sd = tcp_accept(listen_sd);
   ret = read(sd, buf, 4096);
   ret = write(sd, wbuf, sizeof(wbuf));
   // supress compiler warning about unused var
   if (ret)
      ;
   //    tcp_close(sd);
   //    tcp_close(listen_sd);
   exit(0);
}