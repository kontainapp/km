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
 * Kontain Virtual File System
 * ---------------------------
 *
 * KM virtualizes the guest payload's view of it's file environment.
 *
 * KM provides a virtual file system pathname space to the guest process as a root.
 * The virtual filesystem acts like a file system root from the POV of guest, including
 * handling '..' correctly (like Linux pivot_root(2)). This is implemented as file path
 * translations implemented by KM.
 *
 * TODO:
 * - current directory as a clean abslute path name.
 * - current directory in all relative paths from guest.
 * - open safe with symlinks.
 */

#ifndef KM_FILESYS_H_
#define KM_FILESYS_H_

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "km.h"
#include "km_mem.h"
#include "km_syscall.h"

/*
 * Translates a path name from the guest into a KM host pathname.
 * host path points to a char[PATH_MAX].
 */
static inline int km_gpath_to_kpath(km_vcpu_t* vcpu, char* pathname, char* host_path)
{
   char guest_path[PATH_MAX];
   char* cur = host_path;
   int remain = PATH_MAX;
   int inc;

   // Copy pathname.
   if (strlen(pathname) >= sizeof(guest_path)) {
      return -1;
   }
   strncpy(guest_path, pathname, sizeof(guest_path));

   inc = snprintf(cur, remain, "%s", machine.filesys.root_prefix);
   cur += inc;
   remain -= inc;
   if (remain <= 0) {
      return -1;
   }

   int level = 0;
   char* savptr = NULL;
   char* tok = strtok_r(guest_path, "/", &savptr);
   while (tok != NULL) {
      printf("tok=%s\n", tok);
      if (strcmp(tok, "..") == 0) {
         if (level > 0) {
            level--;
            inc = snprintf(cur, remain, "/%s", tok);
            cur += inc;
            remain -= inc;
         }
      } else if (strcmp(tok, ".") != 0) {
         level++;
         inc = snprintf(cur, remain, "/%s", tok);
         cur += inc;
         remain -= inc;
      }
      tok = strtok_r(NULL, "/", &savptr);
   }

   return 0;
}

static inline int check_guest_fd(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= machine.filesys.nfiles) {
      return -EBADF;
   }
   if (__atomic_load_n(&machine.filesys.files[fd].used, __ATOMIC_SEQ_CST) != 0) {
      return fd;
   }
   return -1;
}

/*
 * Note: dup3 will silently close a fd, so if the fd is already in the table, assume
 *       that is what happened.
 */
static inline void add_guest_fd(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd > machine.filesys.nfiles) {
      errx(1, "%s bad file descriptor %d", __FUNCTION__, fd);
   }
   __atomic_store_n(&machine.filesys.files[fd].used, 1, __ATOMIC_SEQ_CST);
   return;
}

static inline void del_guest_fd(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd > machine.filesys.nfiles) {
      errx(1, "%s bad file descriptor %d", __FUNCTION__, fd);
   }
   __atomic_store_n(&machine.filesys.files[fd].used, 0, __ATOMIC_SEQ_CST);
}

static inline int km_init_guest_files(km_machine_init_params_t* params)
{
   struct stat st;
   if (stat(params->rootdir, &st) < 0) {
      err(1, "guest root directory %s", params->rootdir);
   }
   if ((st.st_mode & S_IFDIR) == 0) {
      err(1, "guest root %s is not a directory", params->rootdir);
   }
   machine.filesys.root_prefix = params->rootdir;
   strncpy(machine.filesys.curdir, params->curdir, PATH_MAX - 1);

   struct rlimit lim;
   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -1;
   }
   machine.filesys.files = calloc(lim.rlim_cur, sizeof(struct km_guest_file));
   machine.filesys.nfiles = lim.rlim_cur;
   // stdin, stdout, and stderr are open.
   machine.filesys.files[0].used = 1;
   machine.filesys.files[1].used = 1;
   machine.filesys.files[2].used = 1;
   return 0;
}

// int open(char *pathname, int flags, mode_t mode)
static inline uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode)
{
   // TODO: Preprocess path name
   // TODO: open with O_NOFOLLOW to prevent following symlinks. Symlinks need to be translated
   int fd = __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
   if (fd >= 0) {
      add_guest_fd(vcpu, fd);
   }
   return fd;
}

// int close(fd)
static inline uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   if (check_guest_fd(vcpu, fd) == -1) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_close, fd);
   if (ret == 0) {
      del_guest_fd(vcpu, fd);
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
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(scall, fd, (uintptr_t)buf, count, offset);
   return ret;
}

// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
static inline uint64_t
km_fs_prwv(km_vcpu_t* vcpu, int scall, int fd, const struct iovec* guest_iov, size_t iovcnt, off_t offset)
{
   if (check_guest_fd(vcpu, fd) < 0) {
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
   int ret = __syscall_4(scall, fd, (uintptr_t)iov, iovcnt, offset);

   return ret;
}

// int ioctl(int fd, unsigned long request, void *arg);
static inline uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_ioctl, fd, request, (uintptr_t)arg);
   return ret;
}

// int fcntl(int fd, int cmd, ... /* arg */ );
static inline uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   uint64_t farg = arg;
   if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
      farg = (uint64_t)km_gva_to_kma(arg);
   }
   int ret = __syscall_3(SYS_fcntl, fd, cmd, farg);
   return ret;
}

// off_t lseek(int fd, off_t offset, int whence);
static inline uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_lseek, fd, offset, whence);
   return ret;
}

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
static inline uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_getdents64, fd, (uintptr_t)dirp, count);
   return ret;
}

// int symlink(const char *target, const char *linkpath);
static inline uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath)
{
   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, linkpath, hostpath) < 0) {
      return -ENAMETOOLONG;
   }

   int ret = __syscall_2(SYS_symlink, (uintptr_t)target, (uintptr_t)hostpath);
   return ret;
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
static inline uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz)
{
   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_3(SYS_readlink, (uintptr_t)hostpath, (uintptr_t)buf, bufsz);
   return ret;
}

// int chdir(const char *path);
static inline uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* pathname)
{
   char pwdbuf[PATH_MAX];
   warnx("%s KM pwd:%s", __FUNCTION__, getcwd(pwdbuf, PATH_MAX));
   // Absolute path only (for now)
   if (pathname[0] != '/') {
      warnx("%s %s", __FUNCTION__, "aaa");
      return -EINVAL;
   }

   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      warnx("%s %s", __FUNCTION__, "bbb");
      return -ENAMETOOLONG;
   }

   struct stat st;
   if (stat(hostpath, &st) < 0) {
      warnx("%s %s %s %d", __FUNCTION__, "ccc", hostpath, errno);
      return -errno;
   }
   if ((st.st_mode & S_IFMT) != S_IFDIR) {
      warnx("%s %s", __FUNCTION__, "ddd");
      return -ENOTDIR;
   }
   // TODO: Need to cleanse pathname
   strncpy(machine.filesys.curdir, pathname, PATH_MAX - 1);
   return 0;
}

// int mkdir(const char *path, mode_t mode);
static inline uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   if (pathname == NULL) {
      return -EFAULT;
   }

   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_2(SYS_mkdir, (uintptr_t)hostpath, mode);
   return ret;
}

// int rmdir(const char *path)
static inline uint64_t km_fs_rmdir(km_vcpu_t* vcpu, char* pathname)
{
   if (pathname == NULL) {
      return -EFAULT;
   }

   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_1(SYS_rmdir, (uintptr_t)hostpath);
   return ret;
}

// int rename(const char *oldpath, const char *newpath);
static inline uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   char old_host_path[PATH_MAX];
   char new_host_path[PATH_MAX];

   // TODO: Process oldpath, newpath
   if (km_gpath_to_kpath(vcpu, oldpath, old_host_path) < 0) {
      return -ENAMETOOLONG;
   }
   if (km_gpath_to_kpath(vcpu, newpath, new_host_path) < 0) {
      return -ENAMETOOLONG;
   }

   int ret = __syscall_2(SYS_rename, (uintptr_t)old_host_path, (uintptr_t)new_host_path);
   return ret;
}

// int stat(const char *pathname, struct stat *statbuf);
static inline uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_2(SYS_stat, (uintptr_t)hostpath, (uintptr_t)statbuf);
   return ret;
}

// int lstat(const char *pathname, struct stat *statbuf);
static inline uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_2(SYS_lstat, (uintptr_t)hostpath, (uintptr_t)statbuf);
   return ret;
}

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
static inline uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   char hostpath[PATH_MAX];
   if (km_gpath_to_kpath(vcpu, pathname, hostpath) < 0) {
      return -ENAMETOOLONG;
   }
   int ret = __syscall_5(SYS_statx, dirfd, (uintptr_t)hostpath, flags, mask, (uintptr_t)statxbuf);
   return ret;
}

// int fstat(int fd, struct stat *statbuf);
static inline uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_fstat, fd, (uintptr_t)statbuf);
   return ret;
}

// int dup(int oldfd);
static inline uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_dup, fd);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int dup2(int oldfd, int newfd);
static inline uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   // Don't allow dup to newfd open by KM and not guest.
   struct stat st;
   if (check_guest_fd(vcpu, newfd) == 0 && fstat(newfd, &st) == 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_dup2, fd, newfd);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int dup3(int oldfd, int newfd, int flags);
static inline uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags)
{
   if (check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   // Don't allow dup to newfd open by KM and not guest.
   struct stat st;
   if (check_guest_fd(vcpu, newfd) == 0 && fstat(newfd, &st) == 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_dup2, fd, newfd, flags);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int pipe(int pipefd[2]);
static inline uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   int ret = __syscall_1(SYS_pipe, (uintptr_t)pipefd);
   if (ret == 0) {
      add_guest_fd(vcpu, pipefd[0]);
      add_guest_fd(vcpu, pipefd[1]);
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
static inline uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int ret = __syscall_2(SYS_pipe, (uintptr_t)pipefd, flags);
   if (ret == 0) {
      add_guest_fd(vcpu, pipefd[0]);
      add_guest_fd(vcpu, pipefd[1]);
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
static inline uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
static inline uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
static inline uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_5(SYS_getsockopt, sockfd, level, optname, (uintptr_t)optval, (uintptr_t)optlen);
   return ret;
}

// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
static inline uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_5(SYS_setsockopt, sockfd, level, optname, (uintptr_t)optval, optlen);
   return ret;
}

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static inline uint64_t
km_fs_getsockname(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   if (check_guest_fd(vcpu, sockfd) == -1) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_getsockname, sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
static inline uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_bind, sockfd, (uintptr_t)addr, addrlen);
   return ret;
}

// int listen(int sockfd, int backlog)
static inline uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_listen, sockfd, backlog);
   return ret;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static inline uint64_t
km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_accept, sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
static inline uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_accept4, sockfd, (uintptr_t)addr, (uintptr_t)addrlen, flags);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
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
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_6(SYS_sendto, sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, addrlen);
   return ret;
}

// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
static inline uint64_t km_fs_recvfrom(
    km_vcpu_t* vcpu, int sockfd, void* buf, size_t len, int flags, struct sockaddr* addr, socklen_t* addrlen)
{
   if (check_guest_fd(vcpu, sockfd) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_6(SYS_recvfrom, sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, (uintptr_t)addrlen);
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
   for (int i = 0; i < nfds; i++) {
      if (readfds != NULL && FD_ISSET(i, readfds) && check_guest_fd(vcpu, i) == -1) {
         return -EBADF;
      }
      if (writefds != NULL && FD_ISSET(i, writefds) && check_guest_fd(vcpu, i) == -1) {
         return -EBADF;
      }
      if (exceptfds != NULL && FD_ISSET(i, exceptfds) && check_guest_fd(vcpu, i) == -1) {
         return -EBADF;
      }
   }
   int ret = __syscall_5(SYS_select,
                         nfds,
                         (uintptr_t)readfds,
                         (uintptr_t)writefds,
                         (uintptr_t)exceptfds,
                         (uintptr_t)timeout);
   return ret;
}

// int poll(struct pollfd *fds, nfds_t nfds, int timeout);
static inline uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   if (fds == NULL) {
      return -EINVAL;
   }
   for (int i = 0; i < nfds; i++) {
      if (check_guest_fd(vcpu, fds[i].fd) == -1) {
         return -EBADF;
      }
   }
   int ret = __syscall_3(SYS_poll, (uintptr_t)fds, nfds, timeout);
   return ret;
}

// int epoll_create1(int flags);
static inline uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   int ret = __syscall_1(SYS_epoll_create1, flags);
   if (ret >= 0) {
      add_guest_fd(vcpu, ret);
   }
   return ret;
}

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
static inline uint64_t
km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   if (check_guest_fd(vcpu, epfd) < 0 || check_guest_fd(vcpu, fd) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_epoll_ctl, epfd, op, fd, (uintptr_t)event);
   return ret;
}

// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
//  const sigset_t *sigmask);
static inline uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                                         int epfd,
                                         struct epoll_event* events,
                                         int maxevents,
                                         int timeout,
                                         const sigset_t* sigmask)
{
   if (check_guest_fd(vcpu, epfd) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_5(SYS_epoll_wait, epfd, (uintptr_t)events, maxevents, timeout, (uintptr_t)sigmask);
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
   if (resource == RLIMIT_NOFILE && new_limit != NULL && new_limit->rlim_cur > machine.filesys.nfiles) {
      return -EPERM;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   return ret;
}
#endif