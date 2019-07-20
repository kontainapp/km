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
 * KM file system support
 */

#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "km.h"
#include "km_filesys.h"
#include "km_mem.h"
#include "km_syscall.h"

uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   return __syscall_2(SYS_stat, (uintptr_t)pathname, (uintptr_t)statbuf);
}

uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   return __syscall_5(SYS_statx, dirfd, (uintptr_t)pathname, flags, mask, (uintptr_t)statxbuf);
}

uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   return __syscall_2(SYS_fstat, fd, (uintptr_t)statbuf);
}

uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, int mode)
{
   return __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
}

uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   return __syscall_1(SYS_close, fd);
}

uint64_t km_fs_prw(km_vcpu_t* vcpu, int hc, int fd, void* buf, size_t count, off_t offset)
{
   return __syscall_4(hc, fd, (uintptr_t)buf, count, offset);
}

uint64_t km_fs_prwv(km_vcpu_t* vcpu, int hc, int fd, struct iovec* guest_iov, int cnt, off_t offset)
{
   struct iovec iov[cnt];

   if (guest_iov == NULL) {
      return -EFAULT;
   }

   // ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   // ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   //
   // need to convert not only the address of iov,
   // but also pointers to individual buffers in it
   for (int i = 0; i < cnt; i++) {
      iov[i].iov_base = km_gva_to_kma((long)guest_iov[i].iov_base);
      iov[i].iov_len = guest_iov[i].iov_len;
   }
   return __syscall_4(hc, fd, (uintptr_t)iov, cnt, offset);
}

uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg)
{
   return __syscall_3(SYS_ioctl, fd, request, (uintptr_t)arg);
}

uint64_t km_fs_getdents(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   return __syscall_3(SYS_getdents, fd, (uintptr_t)dirp, count);
}

uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t size)
{
   return __syscall_2(SYS_getcwd, (uintptr_t)buf, size);
}

uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t size)
{
   return __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, size);
}

uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   return __syscall_3(SYS_lseek, fd, offset, whence);
}

uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   return __syscall_2(SYS_rename, (uintptr_t)oldpath, (uintptr_t)newpath);
}

uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* path)
{
   return __syscall_1(SYS_chdir, (uintptr_t)path);
}

uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* path, mode_t mode)
{
   return __syscall_2(SYS_mkdir, (uintptr_t)path, mode);
}

uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   return __syscall_1(SYS_dup, fd);
}

uint64_t km_fs_dup3(km_vcpu_t* vcpu, int oldfd, int newfd, int flags)
{
   return __syscall_3(SYS_dup3, oldfd, newfd, flags);
}

uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   return __syscall_1(SYS_pipe, (uintptr_t)pipefd);
}

uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   return __syscall_2(SYS_pipe, (uintptr_t)pipefd, flags);
}

uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   return __syscall_3(SYS_socket, domain, type, protocol);
}

uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   return __syscall_3(SYS_bind, sockfd, (uintptr_t)addr, addrlen);
}

uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   return __syscall_2(SYS_listen, sockfd, backlog);
}

uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   return __syscall_3(SYS_accept, sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
}

uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
   return __syscall_4(SYS_accept4, sockfd, (uintptr_t)addr, (uintptr_t)addrlen, flags);
}

uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   return __syscall_5(SYS_getsockopt, sockfd, level, optname, (uintptr_t)optval, (uintptr_t)optlen);
}

uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
   return __syscall_5(SYS_setsockopt, sockfd, level, optname, (uintptr_t)optval, optlen);
}

uint64_t km_fs_getsockname(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   return __syscall_3(SYS_getsockname, sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
}

uint64_t km_fs_select(km_vcpu_t* vcpu,
                      int nfds,
                      fd_set* readfds,
                      fd_set* writefds,
                      fd_set* exceptfds,
                      struct timeval* timeout)
{
   return __syscall_5(SYS_select,
                      nfds,
                      (uintptr_t)readfds,
                      (uintptr_t)writefds,
                      (uintptr_t)exceptfds,
                      (uintptr_t)timeout);
}

uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   return __syscall_3(SYS_poll, (uintptr_t)fds, nfds, timeout);
}

uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                      int sockfd,
                      void* buf,
                      size_t len,
                      int flags,
                      struct sockaddr* destaddr,
                      socklen_t addrlen)
{
   return __syscall_6(SYS_sendto, sockfd, (uintptr_t)buf, len, flags, (uintptr_t)destaddr, addrlen);
}

uint64_t km_fs_recvfrom(km_vcpu_t* vcpu,
                        int sockfd,
                        void* buf,
                        size_t len,
                        int flags,
                        struct sockaddr* src_addr,
                        socklen_t* addrlen)
{
   return __syscall_6(SYS_recvfrom, sockfd, (uintptr_t)buf, len, flags, (uintptr_t)src_addr, (uintptr_t)addrlen);
}

uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   return __syscall_1(SYS_epoll_create1, flags);
}

uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   return __syscall_4(SYS_epoll_ctl, epfd, op, fd, (uintptr_t)event);
}

uint64_t km_fs_epoll_pwait(
    km_vcpu_t* vcpu, int epfd, struct epoll_event* events, int maxevents, int timeout, sigset_t* sigmask)
{
   return __syscall_5(SYS_epoll_pwait, epfd, (uintptr_t)events, maxevents, timeout, (uintptr_t)sigmask);
}

uint64_t km_fs_eventfd(km_vcpu_t* vcpu, unsigned int initval, int flags)
{
   return __syscall_2(SYS_eventfd, initval, flags);
}