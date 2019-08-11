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
 */

#ifndef KM_FILESYS_H_
#define KM_FILESYS_H_

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "km.h"
#include "km_mem.h"
#include "km_syscall.h"

static int check_guest_fd(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= machine.filesys.nfdmap) {
      return -EBADF;
   }
   int ret = -1;
   pthread_mutex_lock(&machine.filesys.lock);
   if (machine.filesys.guestfd_to_hostfd_map[fd] != -1) {
      ret = machine.filesys.guestfd_to_hostfd_map[fd];
   }
   pthread_mutex_unlock(&machine.filesys.lock);
   return ret;
}

static int add_guest_fd(km_vcpu_t* vcpu, int host_fd)
{
   if (host_fd < 0 || host_fd > machine.filesys.nfdmap) {
      errx(1, "%s bad file descriptor %d", __FUNCTION__, host_fd);
   }
   int guest_fd = -1;
   pthread_mutex_lock(&machine.filesys.lock);
   for (int i = 0; i < machine.filesys.nfdmap; i++) {
      if (machine.filesys.guestfd_to_hostfd_map[i] == -1) {
         machine.filesys.guestfd_to_hostfd_map[i] = host_fd;
         machine.filesys.guestfd_to_hostfd_map[host_fd] = i;
         guest_fd = i;
         break;
      }
   }
   pthread_mutex_unlock(&machine.filesys.lock);

   return guest_fd;
}

static void del_guest_fd(km_vcpu_t* vcpu, int fd, int hostfd)
{
   if (fd < 0 || fd > machine.filesys.nfdmap) {
      errx(1, "%s bad file descriptor %d", __FUNCTION__, fd);
   }
   pthread_mutex_lock(&machine.filesys.lock);
   machine.filesys.guestfd_to_hostfd_map[fd] = -1;
   machine.filesys.hostfd_to_guestfd_map[hostfd] = -1;
   pthread_mutex_unlock(&machine.filesys.lock);
}

static inline int replace_guest_fd(km_vcpu_t* vcpu, int guest_fd, int host_fd)
{
   if (guest_fd < 0 || guest_fd > machine.filesys.nfdmap) {
      errx(1, "%s bad file descriptor %d", __FUNCTION__, guest_fd);
   }
   int close_fd = -1;
   pthread_mutex_lock(&machine.filesys.lock);
   if (machine.filesys.guestfd_to_hostfd_map[guest_fd] != -1) {
      close_fd = machine.filesys.guestfd_to_hostfd_map[guest_fd];
   }
   machine.filesys.guestfd_to_hostfd_map[guest_fd] = host_fd;
   machine.filesys.hostfd_to_guestfd_map[host_fd] = guest_fd;
   pthread_mutex_unlock(&machine.filesys.lock);
   // don't close stdin, stdout, or stderr
   if (close_fd > 2) {
      __syscall_1(SYS_close, close_fd);

   }
   return guest_fd;
}

// Note: vcpu is NULL if called from km signal handler.
static inline int hostfd_to_guestfd(km_vcpu_t* vcpu, int hostfd)
{
   if (hostfd < 0) {
      return -1;
   }
   int guest_fd = -1;
   pthread_mutex_lock(&machine.filesys.lock);
   guest_fd = machine.filesys.hostfd_to_guestfd_map[hostfd];
   if (machine.filesys.guestfd_to_hostfd_map[guest_fd] != hostfd) {
      pthread_mutex_unlock(&machine.filesys.lock);
      return -1;
   }
   pthread_mutex_unlock(&machine.filesys.lock);
   return guest_fd;
}

static inline int km_fs_init()
{
   struct rlimit lim;

   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -1;
   }

   size_t mapsz = lim.rlim_cur * sizeof(int);

   machine.filesys.guestfd_to_hostfd_map = malloc(mapsz);
   memset(machine.filesys.guestfd_to_hostfd_map, 0xff, mapsz);

   machine.filesys.hostfd_to_guestfd_map = malloc(mapsz);
   memset(machine.filesys.hostfd_to_guestfd_map, 0xff, mapsz);

   machine.filesys.nfdmap = lim.rlim_cur;

   // setup guest std file streams.
   for (int i = 0; i < 3; i++) {
      machine.filesys.guestfd_to_hostfd_map[i] = i;
      machine.filesys.hostfd_to_guestfd_map[i] = i;

   }
   return 0;
}

static inline void km_fs_fini()
{
   if (machine.filesys.guestfd_to_hostfd_map != NULL) {
      free(machine.filesys.guestfd_to_hostfd_map);
      machine.filesys.guestfd_to_hostfd_map = NULL;
   }
   if (machine.filesys.hostfd_to_guestfd_map != NULL) {
      free(machine.filesys.hostfd_to_guestfd_map);
      machine.filesys.hostfd_to_guestfd_map = NULL;
   }
}

// int open(char *pathname, int flags, mode_t mode)
static inline uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode)
{
   int fd = __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
   if (fd >= 0) {
      fd = add_guest_fd(vcpu, fd);
   }
   return fd;
}

// int close(fd)
static inline uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = 0;
   // stdin, stdout, and stderr shared with KM so guest can't close them.
   if (fd > 2) {
      ret = __syscall_1(SYS_close, host_fd);
   } else {
      warnx("guest closing fd=%d", fd);
   }
   if (ret == 0) {
      del_guest_fd(vcpu, fd, host_fd);
   }
   return ret;
}

// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
static inline uint64_t
km_fs_prw(km_vcpu_t* vcpu, int scall, int fd, const void* buf, size_t count, off_t offset)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(scall, host_fd, (uintptr_t)buf, count, offset);
   return ret;
}

// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
static inline uint64_t
km_fs_prwv(km_vcpu_t* vcpu, int scall, int fd, const struct iovec* guest_iov, size_t iovcnt, off_t offset)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   struct iovec iov[iovcnt];

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
   for (int i = 0; i < iovcnt; i++) {
      iov[i].iov_base = km_gva_to_kma((long)guest_iov[i].iov_base);
      iov[i].iov_len = guest_iov[i].iov_len;
   }
   int ret = __syscall_4(scall, host_fd, (uintptr_t)iov, iovcnt, offset);

   return ret;
}

// int ioctl(int fd, unsigned long request, void *arg);
static inline uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_ioctl, host_fd, request, (uintptr_t)arg);
   return ret;
}

// int fcntl(int fd, int cmd, ... /* arg */ );
static inline uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   uint64_t farg = arg;
   if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
      farg = (uint64_t)km_gva_to_kma(arg);
   }
   int ret = __syscall_3(SYS_fcntl, host_fd, cmd, farg);
   return ret;
}

// off_t lseek(int fd, off_t offset, int whence);
static inline uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_lseek, host_fd, offset, whence);
   return ret;
}

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
static inline uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_getdents64, host_fd, (uintptr_t)dirp, count);
   return ret;
}

// int symlink(const char *target, const char *linkpath);
static inline uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath)
{
   int ret = __syscall_2(SYS_symlink, (uintptr_t)target, (uintptr_t)linkpath);
   return ret;
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
static inline uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz)
{
   int ret = __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
   return ret;
}

// int getcwd(char *buf, size_t size);
static inline uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t bufsz)
{
   int ret = __syscall_2(SYS_getcwd, (uintptr_t)buf, bufsz);
   return ret;
}

// int chdir(const char *path);
static inline uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* pathname)
{
   int ret = __syscall_1(SYS_chdir, (uintptr_t)pathname);
   return ret;
}

// int mkdir(const char *path, mode_t mode);
static inline uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   int ret = __syscall_2(SYS_getcwd, (uintptr_t)pathname, mode);
   return ret;
}

// int rename(const char *oldpath, const char *newpath);
static inline uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   int ret = __syscall_2(SYS_rename, (uintptr_t)oldpath, (uintptr_t)newpath);
   return ret;
}

// int stat(const char *pathname, struct stat *statbuf);
static inline uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   int ret = __syscall_2(SYS_stat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int lstat(const char *pathname, struct stat *statbuf);
static inline uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   int ret = __syscall_2(SYS_lstat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
static inline uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   int ret = __syscall_5(SYS_statx, dirfd, (uintptr_t)pathname, flags, mask, (uintptr_t)statxbuf);
   return ret;
}

// int fstat(int fd, struct stat *statbuf);
static inline uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_fstat, host_fd, (uintptr_t)statbuf);
   return ret;
}

// int dup(int oldfd);
static inline uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_dup, host_fd);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int dup2(int oldfd, int newfd);
static inline uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int host_newfd;
   if ((host_newfd = check_guest_fd(vcpu, newfd)) < 0) {
      if ((host_newfd = open("/", O_RDONLY)) < 0) {
         return -errno;
      }
   }
   int ret = __syscall_2(SYS_dup2, host_fd, host_newfd);
   if (ret >= 0) {
      ret = replace_guest_fd(vcpu, newfd, ret);
   }
   return ret;
}

// int dup3(int oldfd, int newfd, int flags);
static inline uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags)
{
   int host_fd;
   if ((host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int host_newfd;
   if ((host_newfd = check_guest_fd(vcpu, newfd)) < 0) {
      if ((host_newfd = open("/", O_RDONLY)) < 0) {
         return -errno;
      }
   }
   int ret = __syscall_3(SYS_dup3, host_fd, host_newfd, flags);
   if (ret >= 0) {
      ret = replace_guest_fd(vcpu, newfd, ret);
   }
   return ret;
}

// int pipe(int pipefd[2]);
static inline uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   int ret = __syscall_1(SYS_pipe, (uintptr_t)pipefd);
   if (ret == 0) {
      pipefd[0] = add_guest_fd(vcpu, pipefd[0]);
      pipefd[1] = add_guest_fd(vcpu, pipefd[1]);
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
static inline uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int ret = __syscall_2(SYS_pipe, (uintptr_t)pipefd, flags);
   if (ret == 0) {
      pipefd[0] = add_guest_fd(vcpu, pipefd[0]);
      pipefd[1] = add_guest_fd(vcpu, pipefd[1]);
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
static inline uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
static inline uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
static inline uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) == -1) {
      return -EBADF;
   }
   int ret =
       __syscall_5(SYS_getsockopt, host_sockfd, level, optname, (uintptr_t)optval, (uintptr_t)optlen);
   return ret;
}

// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
static inline uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) == -1) {
      return -EBADF;
   }
   int ret = __syscall_5(SYS_setsockopt, host_sockfd, level, optname, (uintptr_t)optval, optlen);
   return ret;
}

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static inline uint64_t
km_fs_get_sock_peer_name(km_vcpu_t* vcpu, int hc, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) == -1) {
      return -EBADF;
   }
   int ret = __syscall_3(hc, sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
static inline uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) == -1) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_bind, host_sockfd, (uintptr_t)addr, addrlen);
   return ret;
}

// int listen(int sockfd, int backlog)
static inline uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_listen, host_sockfd, backlog);
   return ret;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static inline uint64_t
km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_accept, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int socketpair(int domain, int type, int protocol, int sv[2]);
static inline uint64_t km_fs_socketpair(km_vcpu_t* vcpu, int domain, int type, int protocol, int sv[2])
{
   int ret = __syscall_4(SYS_socketpair, domain, type, protocol, (uintptr_t)sv);
   if (ret == 0) {
      sv[0] = add_guest_fd(vcpu, sv[0]);
      sv[1] = add_guest_fd(vcpu, sv[1]);
   }
   return ret;
}

// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
static inline uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_accept4, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen, flags);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                const struct sockaddr *dest_addr, socklen_t addrlen);
static inline uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                                    int sockfd,
                                    const void* buf,
                                    size_t len,
                                    int flags,
                                    const struct sockaddr* addr,
                                    socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_6(SYS_sendto, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, addrlen);
   return ret;
}

// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
static inline uint64_t km_fs_recvfrom(
    km_vcpu_t* vcpu, int sockfd, void* buf, size_t len, int flags, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = check_guest_fd(vcpu, sockfd)) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_6(SYS_recvfrom, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

//  int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
static inline uint64_t km_fs_select(km_vcpu_t* vcpu,
                                    int nfds,
                                    fd_set* readfds,
                                    fd_set* writefds,
                                    fd_set* exceptfds,
                                    struct timeval* timeout)
{
   fd_set* host_readfds = NULL;
   fd_set* host_writefds = NULL;
   fd_set* host_exceptfds = NULL;
   fd_set host_readfds_;
   fd_set host_writefds_;
   fd_set host_exceptfds_;

   FD_ZERO(&host_readfds_);
   FD_ZERO(&host_writefds_);
   FD_ZERO(&host_exceptfds_);

   if (readfds != NULL) {
      host_readfds = &host_readfds_;
   }
   if (writefds != NULL) {
      host_writefds = &host_writefds_;
   }
   if (exceptfds != NULL) {
      host_exceptfds = &host_exceptfds_;
   }

   int ret = __syscall_5(SYS_select,
                         nfds,
                         (uintptr_t)host_readfds,
                         (uintptr_t)host_writefds,
                         (uintptr_t)host_exceptfds,
                         (uintptr_t)timeout);
   if (ret > 0) {
      for (int i = 0; i < nfds; i++) {
         int guest_fd = hostfd_to_guestfd(vcpu, i);
         if (guest_fd < 0) {
            continue;
         }
         if (readfds != NULL && FD_ISSET(i, host_readfds)) {
            FD_SET(guest_fd, readfds);
         }
         if (writefds != NULL && FD_ISSET(i, host_writefds)) {
            FD_SET(guest_fd, writefds);
         }
         if (exceptfds != NULL && FD_ISSET(i, host_exceptfds)) {
            FD_SET(guest_fd, exceptfds);
         }
      }
   }
   return ret;
}

// int poll(struct pollfd *fds, nfds_t nfds, int timeout);
static inline uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   struct pollfd host_fds[nfds];

   if (fds == NULL) {
      return -EINVAL;
   }
   for (int i = 0; i < nfds; i++) {
      if ((host_fds[i].fd = check_guest_fd(vcpu, fds[i].fd)) == -1) {
         return -EBADF;
      }
      host_fds[i].events = fds[i].events;
      host_fds[i].revents = 0;
   }
   int ret = __syscall_3(SYS_poll, (uintptr_t)host_fds, nfds, timeout);
   if (ret > 0) {
      for (int i = 0; i < nfds; i++) {
         fds[i].revents = host_fds[i].revents;
      }
   }
   return ret;
}

// int epoll_create1(int flags);
static inline uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   int ret = __syscall_1(SYS_epoll_create1, flags);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
static inline uint64_t
km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   int host_epfd;
   int host_fd;

   if ((host_epfd = check_guest_fd(vcpu, epfd)) < 0 || (host_fd = check_guest_fd(vcpu, fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_epoll_ctl, host_epfd, op, host_fd, (uintptr_t)event);
   return ret;
}

// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
//  const sigset_t *sigmask);
static inline uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                                         int epfd,
                                         struct epoll_event* events,
                                         int maxevents,
                                         int timeout,
                                         const sigset_t* sigmask,
                                         int sigsetsize)
{
   int host_epfd;
   if ((host_epfd = check_guest_fd(vcpu, epfd)) < 0) {
      return -EBADF;
   }

   int ret =
       __syscall_6(SYS_epoll_wait, host_epfd, (uintptr_t)events, maxevents, timeout, (uintptr_t)sigmask, sigsetsize);
   return ret;
}

// int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);
static inline uint64_t km_fs_prlimit64(km_vcpu_t* vcpu,
                                       pid_t pid,
                                       int resource,
                                       const struct rlimit* new_limit,
                                       struct rlimit* old_limit)
{
   /*
    * TODO: A number of rlimit values are effected by KM and we want to virtualize. For example:
    *       RLIMIT_AS - Maximum address space size.
    *       RLIMIT_CORE - Maximum core size.
    *       RLIMIT_DATA - Maximum data a process can create.
    *       RLIMIT_NOFILE - Maximum number of files
    *       RLIMIT_NPROC - Maximum number of processes (should set to 1).
    *       RLIMIT_SIGPENDING - Maximum number of pending signals.
    *       RLIMIT_STACK - maximum size of process stack.
    */
   if (resource == RLIMIT_NOFILE && new_limit != NULL && new_limit->rlim_cur > machine.filesys.nfdmap) {
      return -EPERM;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   return ret;
}
#endif
