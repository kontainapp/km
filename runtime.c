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

#include <unistd.h>
#include <sys/socket.h>

#include "km_hcalls.h"

int errno;

ssize_t write(int fildes, const void *buf, size_t nbyte)
{
   km_hc_args_t arg;

   arg.arg1 = fildes;
   arg.arg2 = (uint64_t)buf;
   arg.arg3 = nbyte;
   return km_hcall(KM_HC_WRITE, &arg);
}

ssize_t read(int fildes, void *buf, size_t nbyte)
{
   km_hc_args_t arg;

   arg.arg1 = fildes;
   arg.arg2 = (uint64_t)buf;
   arg.arg3 = nbyte;
   return km_hcall(KM_HC_READ, &arg);
}

void exit(int status)
{
   km_hc_args_t arg;

   arg.arg1 = status;
   km_hcall(KM_HC_HLT, &arg);
   while (1)
      ;       // squelch ‘noreturn’ function warning
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
   km_hc_args_t arg;

   arg.arg1 = sockfd;
   arg.arg2 = (uint64_t)addr;
   arg.arg3 = (uint64_t)addrlen;
   return km_hcall(KM_HC_ACCEPT, &arg);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
   km_hc_args_t arg;

   arg.arg1 = sockfd;
   arg.arg2 = (uint64_t)addr;
   arg.arg3 = addrlen;
   return km_hcall(KM_HC_BIND, &arg);
}

int listen(int sockfd, int backlog)
{
   km_hc_args_t arg;

   arg.arg1 = sockfd;
   arg.arg2 = backlog;
   return km_hcall(KM_HC_LISTEN, &arg);
}

int socket(int domain, int type, int protocol)
{
   km_hc_args_t arg;

   arg.arg1 = domain;
   arg.arg2 = type;
   arg.arg3 = protocol;
   return km_hcall(KM_HC_SOCKET, &arg);
}

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
   km_hc_args_t arg;

   arg.arg1 = fd;
   arg.arg2 = level;
   arg.arg3 = optname;
   arg.arg4 = (uint64_t)optval;
   arg.arg5 = (uint64_t)optlen;
   return km_hcall(KM_HC_GETSOCKOPT, &arg);
}

int setsockopt(int fd, int level, int optname, const void *optval,
               socklen_t optlen)
{
   km_hc_args_t arg;

   arg.arg1 = fd;
   arg.arg2 = level;
   arg.arg3 = optname;
   arg.arg4 = (uint64_t)optval;
   arg.arg5 = optlen;
   return km_hcall(KM_HC_SETSOCKOPT, &arg);
}

size_t strlen(const char *s)
{
   const char *a = s;

   for (; *s; s++)
      ;
   return s - a;
}

int puts(const char *s)
{
   return write(1, s, strlen(s));
}
