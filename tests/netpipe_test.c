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
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * A small program to convert unix stdin/stdout into network connection
 * endpoints.
 * Usage:
 *  netpipe [-d] { -l addr+port | -c addr+port }
 *  Where the plus sign is literally the seperator between the ip address and
 *  the port number.  addr can also be a dns name and port can be a service
 *  name.
 *
 * netpipe -l creates a listener
 * netpipe -c connects to a listener
 */

#define ADDRDELIMITER '+'

int transfer(int datafd);
int dolisten(char* addrplusport);
int doconnect(char* addrplusport);

int debug = 0;
char* progname;
void usage(void)
{
   fprintf(stderr,
           "usage: %s [-d] { -l addr%cport | -c addr%cport }\n",
           progname,
           ADDRDELIMITER,
           ADDRDELIMITER);
}

int main(int argc, char* argv[])
{
   progname = argv[0];

   if (argc < 2) {
      usage();
      return 1;
   }

   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-l") == 0) {
         dolisten(argv[i + 1]);
         i++;
      } else if (strcmp(argv[i], "-c") == 0) {
         doconnect(argv[i + 1]);
         i++;
      } else if (strcmp(argv[i], "-d") == 0) {
         debug++;
      } else {
         usage();
         return 1;
      }
   }
   return 0;
}

int parseaddrport(char* addrplusport, struct addrinfo** result)
{
   char* temp = strdup(addrplusport);
   struct addrinfo hints = {.ai_flags = AI_PASSIVE,
                            .ai_family = AF_UNSPEC,
                            .ai_socktype = SOCK_STREAM,
                            .ai_protocol = 0};
   *result = NULL;

   char* addr;
   char* port;
   char* p = strchr(temp, ADDRDELIMITER);
   if (p == NULL) {
      // no port, just an address
      addr = temp;
      port = NULL;
   } else if (p == temp) {
      // no address, just a port
      addr = NULL;
      port = p + 1;
   } else {
      // address plus port
      *p = 0;
      addr = temp;
      port = p + 1;
   }
   if (port != NULL && *port == 0) {
      // trailing delimiter and no port
      port = NULL;
   }
   if (addr == NULL && port == NULL) {
      // we have to have something.
      free(temp);
      return EAI_SYSTEM;
   }
   if (debug != 0) {
      fprintf(stdout, "addr = %p <%s>, port = %p <%s>\n", addr, addr, port, port);
   }
   int code = getaddrinfo(addr, port, &hints, result);
   free(temp);
   return code;
}

int dolisten(char* addrplusport)
{
   int error;
   struct addrinfo* result;

   // parse address
   if ((error = parseaddrport(addrplusport, &result)) != 0) {
      fprintf(stderr, "couldn't parse %s, %s\n", addrplusport, gai_strerror(error));
      return 1;
   }

   // create socket and bind local address
   int listenfd = -1;
   for (struct addrinfo* rp = result; rp != NULL; rp = rp->ai_next) {
      listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (listenfd < 0) {
         fprintf(stderr, "bind failed, %s\n", strerror(errno));
         close(listenfd);
         continue;
      }
      int optval = 1;
      if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
         fprintf(stderr, "setsockopt( SO_REUSEADDR ) failed, %s\n", strerror(errno));
         close(listenfd);
         continue;
      }
      if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) == 0) {
         break;
      }
   }
   freeaddrinfo(result);
   if (listenfd < 0) {
      return 1;
   }
   if (listen(listenfd, 1) < 0) {
      fprintf(stderr, "listen failed, %s\n", strerror(errno));
      return 1;
   }

   // accept
   int datafd;
   struct sockaddr sockaddr;
   socklen_t sockaddrlen = sizeof(sockaddr);
   datafd = accept(listenfd, &sockaddr, &sockaddrlen);

   // close listenfd
   close(listenfd);

   // read from network and spew to stdout
   // read from stdin and spew to network
   error = transfer(datafd);

   // close acceptfd
   close(datafd);

   return error;
}

int doconnect(char* addrplusport)
{
   int error;
   struct addrinfo* result;

   // parse address
   if ((error = parseaddrport(addrplusport, &result)) != 0) {
      fprintf(stderr, "couldn't parse %s, %s\n", addrplusport, gai_strerror(error));
      return 1;
   }

   // create socket and connect to remote address
   int connectfd = -1;
   for (struct addrinfo* rp = result; rp != NULL; rp = rp->ai_next) {
      connectfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (connectfd >= 0) {
         if (connect(connectfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
         }
         fprintf(stderr, "connect failed, %s\n", strerror(errno));
         close(connectfd);
         connectfd = -1;
      }
   }
   freeaddrinfo(result);
   if (connectfd < 0) {
      return 1;
   }

   // read from network and spew to stdout
   // read from stdin and spew to network
   error = transfer(connectfd);

   close(connectfd);

   return error;
}

/*
 * Read from the datafd and write to stdout.
 * Read from stdin and write to datafd.
 * Keep going until any error or both data sources are at eof.
 */
int transfer(int datafd)
{
   int eof_on_stdin = 0;
   int eof_on_datafd = 0;

   while (eof_on_stdin == 0 || eof_on_datafd == 0) {
      uint8_t buffer[16 * 1024];
      ssize_t bytesread;
      ssize_t byteswritten;
      ssize_t count;

      struct pollfd fds[2];
      fds[0].fd = eof_on_datafd != 0 ? -1 : datafd;
      fds[0].events = POLLIN;
      fds[0].revents = 0;
      fds[1].fd = eof_on_stdin != 0 ? -1 : fileno(stdin);
      fds[1].events = POLLIN;
      fds[1].revents = 0;
      if (poll(fds, 2, -1) < 0) {
         fprintf(stderr, "poll() failed, %s\n", strerror(errno));
         return 1;
      }

      if (debug != 0) {
         fprintf(stderr, "poll revents network fd 0x%x, stdin fd 0x%x\n", fds[0].revents, fds[1].revents);
      }

      // read whatever we can from the socket and write to stdout
      if ((fds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
         bytesread = recv(datafd, buffer, sizeof(buffer), 0);
         if (debug != 0) {
            fprintf(stderr, "bytes read from network %ld\n", bytesread);
         }
         if (bytesread == 0) {
            // connection closed
            eof_on_datafd = 1;
            if (debug != 0) {
               fprintf(stdout, "End of file on network endpoint\n");
            }
         } else if (bytesread < 0) {
            fprintf(stderr, "read from network failed, %s\n", strerror(errno));
            return 1;
         }
         // make sure it all goes to stdout
         size_t byteswritten = 0;
         while (byteswritten < bytesread) {
            count = fwrite(&buffer[byteswritten], 1, bytesread - byteswritten, stdout);
            if (ferror(stdout)) {
               // not really sure if fwrite or ferror leave anything useful in errno, but try to
               // give a useful error message.
               fprintf(stderr, "fwrite to stdout failed, %s\n", strerror(errno));
               return 1;
            }
            byteswritten += count;
         }
         fflush(stdout);
      }

      // read from stdin and write to datafd.
      if ((fds[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
         bytesread = read(fileno(stdin), buffer, sizeof(buffer));
         if (debug != 0) {
            fprintf(stderr, "bytes read from stdin %ld\n", bytesread);
         }
         if (bytesread < 0) {
            fprintf(stderr, "error reading from stdin, %s\n", strerror(errno));
            return 1;
         }
         if (bytesread == 0) {
            eof_on_stdin = 1;
            shutdown(datafd, SHUT_WR);
            if (debug != 0) {
               fprintf(stdout, "End of file on stdin\n");
            }
         }
         // make sure it all goes into the datafd
         byteswritten = 0;
         while (byteswritten < bytesread) {
            count = send(datafd, &buffer[byteswritten], bytesread - byteswritten, MSG_NOSIGNAL);
            if (count < 0) {
               if (errno == EPIPE) {
                  // other end closed connection on us.
                  eof_on_stdin = 1;
                  eof_on_datafd = 1;
                  break;
               } else {
                  // a real error
                  fprintf(stderr, "write to network failed, %s\n", strerror(errno));
                  return 1;
               }
            }
            byteswritten += count;
         }
      }
   }
   return 0;
}
