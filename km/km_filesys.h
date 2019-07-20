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
 * KM functions for guest syscalls that involve pathnames and/or file descriptors
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km.h"

/*
 * File system calls.
 */
// int stat(const char *pathname, struct stat *statbuf)
uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf);

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf);

// int fstat(int fd, struct stat *statbuf);
uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf);

// int open(const char *pathname, int flags, mode_t mode)
uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathnname, int flags, int mode);

// int close(int fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd);

// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
uint64_t km_fs_prw(km_vcpu_t* vcpu, int hc, int fd, void* buf, size_t count, off_t offset);

// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
uint64_t km_fs_prwv(km_vcpu_t* vcpu, int hc, int fd, struct iovec* iov, int iovcnt, off_t offset);

// int ioctl(int fd, unsigned long request, void *arg)
uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg);

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count)
uint64_t km_fs_getdents(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count);

// int getcwd(char *buf, size_t size)
uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t size);

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t size);

// off_t lseek(int fd, off_t offset, int whence)
uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence);

// int rename(const char *oldpath, const char *newpath)
uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath);

// int chdir(const char *path)
uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* path);

// int mkdir(const char *path, mode_t mode)
uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* path, mode_t mode);

// int dup(int oldfd)
uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd);

// int dup3(int oldfd, int newfd, int flags)
uint64_t km_fs_dup3(km_vcpu_t* vcpu, int oldfd, int newfd, int flags);

/*
 * pipe/socket calls
 */

// int pipe(int pipefd[2])
uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2]);

// int pipe2(int pipefd[2], int flags)
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefs[2], int flags);

// int socket(int domain, int type, int protocol)
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol);

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen);

// int listen(int sockfd, int backlog)
uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog);

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen);

// int accept4(int sockfd, struct sockaddr *addr,
//             socklen_t *addrlen, int flags);
uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags);

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t
// *optlen)
uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen);

// int setsockopt(int sockfd, int level, int optname, const void *optval,
// socklen_t optlen)
uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen);

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
uint64_t km_fs_getsockname(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen);

//  int select(int nfds, fd_set *readfds, fd_set *writefds,
//             fd_set *exceptfds, struct timeval *timeout)
uint64_t km_fs_select(km_vcpu_t* vcpu,
                      int nfds,
                      fd_set* readfds,
                      fd_set* writefds,
                      fd_set* exceptfds,
                      struct timeval* timeout);

// int poll(struct pollfd *fds, nfds_t nfds, int timeout)
uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout);

// ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                const struct sockaddr *dest_addr, socklen_t addrlen)
uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                      int sockfd,
                      void* buf,
                      size_t len,
                      int flags,
                      struct sockaddr* destaddr,
                      socklen_t addrlen);

// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen)
uint64_t km_fs_recvfrom(km_vcpu_t* vcpu,
                        int sockfd,
                        void* buf,
                        size_t len,
                        int flags,
                        struct sockaddr* src_addr,
                        socklen_t* addrlen);

// int epoll_create1(int flags)
uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags);

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event);

// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const
// sigset_t *sigmask)
uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                           int epfd,
                           struct epoll_event* events,
                           int maxevents,
                           int timeout,
                           sigset_t* sigmask);

// int eventfd(unsigned int initval, int flags)
uint64_t km_fs_eventfd(km_vcpu_t* vcpu, unsigned int initval, int flags);