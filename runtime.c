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

static const char *msg = "Hello, world\n";

static inline void km_hcall(int n, volatile void *arg)
{
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
}

ssize_t write(int fildes, const void *buf, size_t nbyte)
{
   km_rw_hc_t arg = {
       .fd = fildes,
       .r_w = WRITE,
       .data = (uint64_t)buf,
       .length = nbyte,
   };

   km_hcall(KM_HC_RW, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
}

ssize_t read(int fildes, void *buf, size_t nbyte)
{
   km_rw_hc_t arg = {
       .fd = fildes,
       .r_w = READ,
       .data = (uint64_t)buf,
       .length = nbyte,
   };

   km_hcall(KM_HC_RW, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
}

void exit(int status)
{
   km_hlt_hc_t arg = {.exit_code = status};

   km_hcall(KM_HC_HLT, &arg);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
   km_accept_hc_t arg = {
       .sockfd = sockfd,
       .addr = (uint64_t)addr,
   };

   km_hcall(KM_HC_ACCEPT, &arg);
   *addrlen = arg.addrlen;
   errno = arg.hc_errno;
   return arg.hc_ret;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
   km_bind_hc_t arg = {
       .sockfd = sockfd,
       .addr = (uint64_t)addr,
       .addrlen = addrlen,
   };

   km_hcall(KM_HC_BIND, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
}

int listen(int sockfd, int backlog)
{
   km_listen_hc_t arg = {
       .sockfd = sockfd,
       .backlog = backlog,
   };

   km_hcall(KM_HC_LISTEN, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
}

int socket(int domain, int type, int protocol)
{
   km_socket_hc_t arg = {
       .domain = domain,
       .type = type,
       .protocol = protocol,
   };

   km_hcall(KM_HC_SOCKET, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
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
