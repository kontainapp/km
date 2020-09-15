/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Notes on File Descriptors
 * -------------------------
 * Payload file descriptors are numerically preserved.
 * KM files are duped to the upper area (MAX_OPEN_FILES - MAX_KM_FILES) so there is no conflict.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_mem.h"
#include "km_snapshot.h"
#include "km_syscall.h"

static const int MAX_OPEN_FILES = 1024;
static const int MAX_KM_FILES = KVM_MAX_VCPUS + 2 + 2 + 2 + 2 + 1;   // eventfds, kvm, gdb, snap, log

static const int KM_GDB_LISTEN = MAX_OPEN_FILES - MAX_KM_FILES;
static const int KM_GDB_ACCEPT = MAX_OPEN_FILES - MAX_KM_FILES + 1;
static const int KM_MGM_LISTEN = MAX_OPEN_FILES - MAX_KM_FILES + 2;
static const int KM_MGM_ACCEPT = MAX_OPEN_FILES - MAX_KM_FILES + 3;
static const int KM_LOGGING = MAX_OPEN_FILES - MAX_KM_FILES + 4;
static const int KM_START_FDS = MAX_OPEN_FILES - MAX_KM_FILES + 5;

static const_string_t stdin_name = "[stdin]";
static const_string_t stdout_name = "[stdout]";
static const_string_t stderr_name = "[stderr]";

static char proc_pid_fd[128];
static char proc_pid_exe[128];
static char proc_pid[128];
static int proc_pid_length;

static char* km_my_exec;   // my executable per /proc/self/exe to check with in readlink

typedef struct km_fd_socket {
   int state;
   int backlog;
   int domain;
   int type;
   int protocol;
   // currently all linux sockaddr variations fit in 128 bytes
   int addrlen;
   char addr[128];
} km_fd_socket_t;

#define KM_SOCK_STATE_OPEN 0
#define KM_SOCK_STATE_BIND 1
#define KM_SOCK_STATE_LISTEN 2
#define KM_SOCK_STATE_ACCEPT 3
#define KM_SOCK_STATE_CONNECT 4

typedef struct km_fs_event {
   TAILQ_ENTRY(km_fs_event) link;
   int fd;
   struct epoll_event event;
} km_fs_event_t;

typedef struct km_file {
   int inuse;
   int how;              // How was this file created
   int flags;            // Open flags
   km_file_ops_t* ops;   // Overwritten file ops for file matched at open
   int ofd;              // 'other' fd (pipe and socketpair)
   char* name;
   km_fd_socket_t* sockinfo;           // For sockets
   TAILQ_HEAD(, km_fs_event) events;   // for epoll_create fd's
} km_file_t;

#define KM_FILE_HOW_OPEN 0    /* Regular open */
#define KM_FILE_HOW_PIPE_0 1  /* read half of pipe */
#define KM_FILE_HOW_PIPE_1 2  /* write half of pipe */
#define KM_FILE_HOW_EVENTFD 3 /* epoll_create */
#define KM_FILE_HOW_SOCKET 4
#define KM_FILE_HOW_ACCEPT 5
#define KM_FILE_HOW_SOCKETPAIR0 6
#define KM_FILE_HOW_SOCKETPAIR1 7
#define KM_FILE_HOW_RECVMSG 8

typedef struct km_filesys {
   int nfdmap;               // size of file descriptor maps
   km_file_t* guest_files;   // Indexed by guestfd
} km_filesys_t;

// file name conversion functions forward declarations
static int km_fs_g2h_filename(const char* name, char* buf, size_t bufsz, km_file_ops_t** ops);
static int km_fs_g2h_readlink(const char* name, char* buf, size_t bufsz);

static inline km_filesys_t* km_fs()
{
   return machine.filesys;
}

/*
 * Get file name for file descriptors that are not files - socket, pipe, and such
 */
static char* km_get_nonfile_name(int hostfd)
{
   char buf[PATH_MAX + 1];
   char fn[128];
   snprintf(fn, sizeof(fn), PROC_SELF_FD, hostfd);

   int ret = readlink(fn, buf, PATH_MAX);
   if (ret < 0) {
      return NULL;
   }
   return strndup(buf, ret);
}

/*
 * Adds a host fd to the guest. Returns the guest fd number assigned.
 * Assigns lowest available guest fd, just like the kernel.
 * TODO: Support open flags (O_CLOEXEC in particular)
 */
int km_add_guest_fd_internal(km_vcpu_t* vcpu, int host_fd, char* name, int flags, int how, km_file_ops_t* ops)
{
   assert(host_fd >= 0 && host_fd < km_fs()->nfdmap);
   int available = 0;
   int taken = 1;
   if (__atomic_compare_exchange_n(&km_fs()->guest_files[host_fd].inuse,
                                   &available,
                                   taken,
                                   0,
                                   __ATOMIC_SEQ_CST,
                                   __ATOMIC_SEQ_CST) == 0) {
      km_errx(0, "file slot %d is taken unexpectedly", host_fd);
   }
   km_file_t* file = &km_fs()->guest_files[host_fd];
   file->ops = ops;
   file->how = how;
   file->ofd = -1;
   file->sockinfo = NULL;
   TAILQ_INIT(&file->events);
   if (name == NULL) {
      file->name = km_get_nonfile_name(host_fd);
   } else {
      file->name = strdup(name);
   }
   file->flags = flags;
   return host_fd;
}

int km_add_guest_fd(km_vcpu_t* vcpu, int host_fd, char* name, int flags, km_file_ops_t* ops)
{
   return km_add_guest_fd_internal(vcpu, host_fd, name, flags, KM_FILE_HOW_OPEN, ops);
}

static inline void km_connect_files(km_vcpu_t* vcpu, int guestfd[2])
{
   km_fs()->guest_files[guestfd[0]].ofd = guestfd[1];
   km_fs()->guest_files[guestfd[1]].ofd = guestfd[0];
}

static inline int km_add_socket_fd(
    km_vcpu_t* vcpu, int hostfd, char* name, int flags, int domain, int type, int protocol, int how)
{
   int ret = km_add_guest_fd_internal(vcpu, hostfd, name, flags, how, NULL);
   if (ret >= 0) {
      km_fd_socket_t sockval = {.domain = domain, .type = type, .protocol = protocol};
      km_fd_socket_t* sockinfo = calloc(1, sizeof(km_fd_socket_t));
      *sockinfo = sockval;
      km_fs()->guest_files[ret].sockinfo = sockinfo;
   }
   return ret;
}

static inline void km_disconnect_file(km_vcpu_t* vcpu, int fd)
{
   km_file_t* file = &km_fs()->guest_files[fd];
   if (file->ofd != -1) {
      km_file_t* other = &km_fs()->guest_files[file->ofd];
      assert(other->ofd == fd);
      other->ofd = -1;
      file->ofd = -1;
   }
}

/*
 * deletes an exist guestfd to hostfd mapping (used by km_fs_close())
 */
static inline void del_guest_fd(km_vcpu_t* vcpu, int fd)
{
   assert(fd >= 0 && fd < km_fs()->nfdmap);
   km_file_t* file = &km_fs()->guest_files[fd];
   if (__atomic_exchange_n(&file->inuse, 0, __ATOMIC_SEQ_CST) != 0) {
      file->ops = NULL;
      if (file->name != NULL) {
         free(file->name);
         file->name = NULL;
      }
   }
   if (file->sockinfo != NULL) {
      free(file->sockinfo);
      file->sockinfo = NULL;
   }
   km_disconnect_file(vcpu, fd);
}

char* km_guestfd_name(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= km_fs()->nfdmap) {
      return NULL;
   }
   return km_fs()->guest_files[fd].name;
}

/*
 * maps a host fd to a guest fd. Returns a negative error number if mapping does not exist. Used by
 * SIGPIPE/SIGIO signal handlers and select. Note: vcpu is NULL if called from km signal handler.
 */
int km_fs_h2g_fd(int hostfd)
{
   if (hostfd < 0 || hostfd >= km_fs()->nfdmap) {
      return -ENOENT;
   }
   if (__atomic_load_n(&km_fs()->guest_files[hostfd].inuse, __ATOMIC_SEQ_CST) == 0) {
      return -ENOENT;
   }
   return hostfd;
}

/*
 * Translates guest fd to host fd. Returns negative errno if mapping does not exist.
 */
int km_fs_g2h_fd(int fd, km_file_ops_t** ops)
{
   if (fd < 0 || fd >= km_fs()->nfdmap) {
      return -1;
   }
   if (__atomic_load_n(&km_fs()->guest_files[fd].inuse, __ATOMIC_SEQ_CST) == 0) {
      return -1;
   }
   if (ops != NULL) {
      *ops = km_fs()->guest_files[fd].ops;
   }
   return fd;
}

int km_fs_max_guestfd()
{
   return km_fs()->nfdmap;
}

// int open(char *pathname, int flags, mode_t mode)
uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode)
{
   char buf[PATH_MAX];
   km_file_ops_t* ops;

   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   int hostfd = __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
   int guestfd;
   if (hostfd >= 0) {
      guestfd = km_add_guest_fd_internal(vcpu, hostfd, pathname, flags, KM_FILE_HOW_OPEN, ops);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "open(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

uint64_t km_fs_openat(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, mode_t mode)
{
   int host_dirfd = dirfd;
   km_file_ops_t* ops;

   if (dirfd != AT_FDCWD && pathname[0] != '/') {
      if ((host_dirfd = km_fs_g2h_fd(dirfd, &ops)) < 0) {
         return -EBADF;
      }
      if (ops != NULL && ops->getdents_g2h != NULL) {
         km_warnx("bad dirfd in openat");
         return -EINVAL;   // no openat with base in /proc and such
      }
   }
   char buf[PATH_MAX];

   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   int hostfd = __syscall_4(SYS_openat, host_dirfd, (uintptr_t)pathname, flags, mode);
   int guestfd;
   if (hostfd >= 0) {
      guestfd = km_add_guest_fd_internal(vcpu, hostfd, pathname, flags, KM_FILE_HOW_OPEN, ops);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "openat(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

// int close(fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   km_infox(KM_TRACE_FILESYS, "close(%d)", fd);
   if (km_fs_g2h_fd(fd, NULL) < 0) {
      return -EBADF;
   }
   /*
    * Notes on closing guestfd:
    *
    * The "Dealing with error returns from close()" section
    * of the close(2) man page states: "Linux kernel always releases the
    * file descriptor early in the close operation, freeing it for reuse".
    * (And when they say 'always' they mean always. While close(2) can return
    * an error, the file descriptor is unconditionally closed).
    *
    * Hence the hostfd/guestfd mappings are cleared unconditionally
    * before close(2) is called.
    */
   del_guest_fd(vcpu, fd);
   int ret = __syscall_1(SYS_close, fd);
   if (ret != 0) {
      km_warn(" error return from close of guest fd %d", fd);
   }
   return ret;
}

// int shutdown(int sockfd, int how);
int km_fs_shutdown(km_vcpu_t* vcpu, int sockfd, int how)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_shutdown, host_fd, how);
   return ret;
}

// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
uint64_t km_fs_prw(km_vcpu_t* vcpu, int scall, int fd, void* buf, size_t count, off_t offset)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret;
   if (ops != NULL && ops->read_g2h != NULL && (scall == SYS_read || scall == SYS_pread64)) {
      if (scall == SYS_pread64 && offset != 0) {
         km_warnx("unsupported %s", km_hc_name_get(scall));
         return -EINVAL;
      }
      ret = ops->read_g2h(host_fd, buf, count);
   } else {
      ret = __syscall_4(scall, host_fd, (uintptr_t)buf, count, offset);
   }
   return ret;
}

// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
uint64_t
km_fs_prwv(km_vcpu_t* vcpu, int scall, int fd, const struct iovec* guest_iov, size_t iovcnt, off_t offset)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   if (ops != NULL && ops->read_g2h != NULL && (scall == SYS_readv || scall == SYS_preadv)) {
      km_warnx("unsupported %s", km_hc_name_get(scall));
      return -EINVAL;
   }
   // ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   // ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   //
   // need to convert not only the address of iov,
   // but also pointers to individual buffers in it
   struct iovec iov[iovcnt];
   for (int i = 0; i < iovcnt; i++) {
      iov[i].iov_base = km_gva_to_kma((long)guest_iov[i].iov_base);
      iov[i].iov_len = guest_iov[i].iov_len;
   }
   int ret = __syscall_4(scall, host_fd, (uintptr_t)iov, iovcnt, offset);
   return ret;
}

// int ioctl(int fd, unsigned long request, void *arg);
uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_ioctl, host_fd, request, (uintptr_t)arg);
   return ret;
}

// int fcntl(int fd, int cmd, ... /* arg */ );
uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   uint64_t farg = arg;
   if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
      farg = (uint64_t)km_gva_to_kma(arg);
   } else if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      // Let kernel pick hostfd destination. Satisfy the fd number request for guest below.
      farg = arg;
   }
   int ret = __syscall_3(SYS_fcntl, host_fd, cmd, farg);
   if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      if (ret >= 0) {
         km_file_t* file = &km_fs()->guest_files[fd];
         ret = km_add_guest_fd_internal(vcpu,
                                        ret,
                                        km_guestfd_name(vcpu, fd),
                                        (cmd == F_DUPFD) ? 0 : O_CLOEXEC,
                                        file->how,
                                        ops);
      }
   }
   return ret;
}

// off_t lseek(int fd, off_t offset, int whence);
uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   if (ops != NULL && ops->read_g2h != NULL && whence != SEEK_SET && offset != 0) {
      /*
       * File was matched on open with read op overwritten. Currently there are two,
       * /proc/(self|getpid())/sched. It's hard to keep track of file pointer, and nobody does lseek
       * other than rewind/lseek(0)
       */
      km_warnx("unsupported lseek on %s", km_guestfd_name(vcpu, fd));
      return -EINVAL;
   }
   int ret = __syscall_3(SYS_lseek, host_fd, offset, whence);
   return ret;
}

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret;
   if (ops != NULL && ops->getdents_g2h != NULL) {
      ret = ops->getdents_g2h(host_fd, dirp, count);
   } else {
      ret = __syscall_3(SYS_getdents64, host_fd, (uintptr_t)dirp, count);
   }
   return ret;
}

// int symlink(const char *target, const char *linkpath);
uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath)
{
   int ret = km_fs_g2h_filename(linkpath, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad linkpath %s in symlink", linkpath);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(target, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad target %s in symlink", target);
      return -EINVAL;
   }
   ret = __syscall_2(SYS_symlink, (uintptr_t)target, (uintptr_t)linkpath);
   return ret;
}

// int link(const char *oldpath, const char *newpath);
uint64_t km_fs_link(km_vcpu_t* vcpu, char* old, char* new)
{
   int ret = km_fs_g2h_filename(old, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad oldpath %s in rename", old);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(new, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad new %s in rename", new);
      return -EINVAL;
   }
   ret = __syscall_2(SYS_link, (uintptr_t)old, (uintptr_t) new);
   return ret;
}

/*
 * check if pathname is in "/proc/self/fd/" or "/proc/`getpid()`/fd", process if it is.
 * Return 0 if doesn't match, negative for error, positive for result strlen
 */
static int proc_self_fd_name(const char* pathname, char* buf, size_t bufsz)
{
   int fd;
   if (sscanf(pathname, PROC_SELF_FD, &fd) != 1 && sscanf(pathname, proc_pid_fd, &fd) != 1) {
      return -ENOENT;
   }
   char* mpath;
   if ((mpath = km_guestfd_name(NULL, fd)) == NULL) {
      return -ENOENT;
   }
   /*
    * /proc/self/fd symlinks contain full path name of file, so try to get that.
    */
   char* rpath = realpath(mpath, NULL);
   if (rpath != NULL) {
      mpath = rpath;
   }
   strncpy(buf, mpath, bufsz);
   int ret = strlen(mpath);
   if (ret > bufsz) {
      ret = bufsz;
   }
   if (rpath != NULL) {
      free(rpath);
   }
   return ret;
}

/*
 * check if pathname is in "/proc/self/exe" or "/proc/`getpid()`/exe", process if it is.
 * Return 0 if doesn't match, negative for error, positive for result strlen
 */
static int proc_self_exe_name(const char* pathname, char* buf, size_t bufsz)
{
   strncpy(buf, km_guest.km_filename, bufsz);
   int ret = strlen(km_guest.km_filename);
   if (ret > bufsz) {
      ret = bufsz;
   }
   return ret;
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz)
{
   int ret;
   if ((ret = km_fs_g2h_readlink(pathname, buf, bufsz)) == 0) {
      char tmp[PATH_MAX + 1];
      if (realpath(pathname, tmp) != NULL && strcmp(tmp, km_my_exec) == 0) {
         strncpy(buf, km_guest.km_filename, bufsz);
         if ((ret = strlen(km_guest.km_filename)) > bufsz) {
            ret = bufsz;
         }
      } else {
         ret = __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
      }
   }
   if (ret < 0) {
      km_infox(KM_TRACE_FILESYS, "%s : %s", pathname, strerror(-ret));
   } else {
      km_infox(KM_TRACE_FILESYS, "%s -> %.*s", pathname, ret, buf);
   }
   return ret;
}

// ssize_t readlinkat(it dirfd, const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlinkat(km_vcpu_t* vcpu, int dirfd, char* pathname, char* buf, size_t bufsz)
{
   int ret;
   km_file_ops_t* ops;

   if ((ret = km_fs_g2h_readlink(pathname, buf, bufsz)) == 0) {
      int host_dirfd = dirfd;

      if (dirfd != AT_FDCWD && pathname[0] != '/') {
         if ((host_dirfd = km_fs_g2h_fd(dirfd, &ops)) < 0) {
            return -EBADF;
         }
         if (ops != NULL) {
            km_warnx("bad dirfd in readlinkat");
            return -EINVAL;   // no redlinkat with base in /proc and such
         }
      }
      ret = __syscall_4(SYS_readlinkat, host_dirfd, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
   }
   if (ret < 0) {
      km_infox(KM_TRACE_FILESYS, "%s : %s", pathname, strerror(-ret));
   } else {
      km_infox(KM_TRACE_FILESYS, "%s -> %.*s", pathname, ret, buf);
   }
   return ret;
}

// int getcwd(char *buf, size_t size);
uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t bufsz)
{
   int ret = __syscall_2(SYS_getcwd, (uintptr_t)buf, bufsz);
   return ret;
}

// int chdir(const char *path);
uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* pathname)
{
   int ret = km_fs_g2h_filename(pathname, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad pathname %s in chdir", pathname);
      return -EINVAL;
   }
   ret = __syscall_1(SYS_chdir, (uintptr_t)pathname);
   return ret;
}

// int fchdir(int fd);
uint64_t km_fs_fchdir(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   if (ops != NULL) {
      km_warnx("bad fd in fchdir");
      return -EINVAL;   // no fchdir with base in /proc and such
   }
   int ret = __syscall_1(SYS_fchdir, host_fd);
   return ret;
}

// int truncate(const char *path, off_t length);
uint64_t km_fs_truncate(km_vcpu_t* vcpu, char* pathname, off_t length)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_truncate, (uintptr_t)pathname, length);
   return ret;
}

// int ftruncate(int fd, off_t length);
uint64_t km_fs_ftruncate(km_vcpu_t* vcpu, int fd, off_t length)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_ftruncate, host_fd, length);
   return ret;
}

// int fsync(int fd);
uint64_t km_fs_fsync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_fsync, host_fd);
   return ret;
}

// int fdatasync(int fd);
uint64_t km_fs_fdatasync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_fdatasync, host_fd);
   return ret;
}

// int mkdir(const char *path, mode_t mode);
uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_mkdir, (uintptr_t)pathname, mode);
   return ret;
}

// int rmdir(const char *path);
uint64_t km_fs_rmdir(km_vcpu_t* vcpu, char* pathname)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_1(SYS_rmdir, (uintptr_t)pathname);
   return ret;
}

// int unlink(const char *path);
uint64_t km_fs_unlink(km_vcpu_t* vcpu, char* pathname)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_1(SYS_unlink, (uintptr_t)pathname);
   return ret;
}

// int mknod(const char *pathname, mode_t mode, dev_t dev);
uint64_t km_fs_mknod(km_vcpu_t* vcpu, char* pathname, mode_t mode, dev_t dev)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_mknod, (uintptr_t)pathname, mode, dev);
   return ret;
}

// int chown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_chown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_chown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int lchown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_lchown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_lchown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int fchown(int fd, uid_t owner, gid_t group);
uint64_t km_fs_fchown(km_vcpu_t* vcpu, int fd, uid_t uid, gid_t gid)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_fchown, host_fd, uid, gid);
   return ret;
}

// int chmod(const char *pathname, mode_t mode);
uint64_t km_fs_chmod(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_chmod, (uintptr_t)pathname, mode);
   return ret;
}

// int fchmod(int fd, mode_t mode);
uint64_t km_fs_fchmod(km_vcpu_t* vcpu, int fd, mode_t mode)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_fchmod, host_fd, mode);
   return ret;
}

// int rename(const char *roundup(strlenoldpath, const char *newpath);
uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   int ret = km_fs_g2h_filename(oldpath, NULL, 0, NULL);
   if (ret < 0) {
      return ret;
   }
   ret = km_fs_g2h_filename(newpath, NULL, 0, NULL);
   if (ret < 0) {
      return ret;
   }
   ret = __syscall_2(SYS_rename, (uintptr_t)oldpath, (uintptr_t)newpath);
   return ret;
}

// int stat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_stat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int lstat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_lstat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   int host_fd = dirfd;
   km_file_ops_t* ops;

   if (dirfd != AT_FDCWD && pathname[0] != '/') {
      if ((host_fd = km_fs_g2h_fd(dirfd, &ops)) < 0) {
         return -EBADF;
      }
      if (ops != NULL && ops->readlink_g2h != NULL) {
         km_warnx("bad dirfd in statx");
         return -EINVAL;   // no statx with base in /proc and such
      }
   }
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_5(SYS_statx, host_fd, (uintptr_t)pathname, flags, mask, (uintptr_t)statxbuf);
   return ret;
}

// int fstat(int fd, struct stat *statbuf);
uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_fstat, host_fd, (uintptr_t)statbuf);
   if (km_trace_enabled() != 0) {
      char* file_name = km_guestfd_name(vcpu, fd);
      km_infox(KM_TRACE_FILESYS, "%s guest fd=%d hostfd=%d ret=%d", file_name, fd, host_fd, ret);
   }

   return ret;
}

// int statfs(int fd, struct statfs *statbuf);
uint64_t km_fs_statfs(km_vcpu_t* vcpu, char* pathname, struct statfs* statbuf)
{
   uint64_t ret = __syscall_2(SYS_statfs, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int fstatfs(int fd, struct statfs *statbuf);
uint64_t km_fs_fstatfs(km_vcpu_t* vcpu, int fd, struct statfs* statbuf)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   uint64_t ret = __syscall_2(SYS_fstatfs, host_fd, (uintptr_t)statbuf);
   return ret;
}

// int access(const char *pathname, int mode);
uint64_t km_fs_access(km_vcpu_t* vcpu, const char* pathname, int mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_access, (uintptr_t)pathname, mode);
   return ret;
}

// int dup(int oldfd);
uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   char* name = km_guestfd_name(vcpu, fd);
   assert(name != NULL);
   int ret = __syscall_1(SYS_dup, host_fd);
   if (ret >= 0) {
      km_file_t* file = &km_fs()->guest_files[fd];
      ret = km_add_guest_fd_internal(vcpu, ret, name, 0, file->how, ops);
   }
   km_infox(KM_TRACE_FILESYS, "dup(%d) - %d", fd, ret);
   return ret;
}

// int dup3(int oldfd, int newfd, int flags);
uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags)
{
   if (fd == newfd) {
      return -EINVAL;
   }
   if ((flags & ~O_CLOEXEC) != 0) {
      return -EINVAL;
   }
   if (newfd < 0 || newfd >= km_fs()->nfdmap) {
      return -EBADF;
   }

   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }

   char* name = km_guestfd_name(vcpu, fd);
   assert(name != NULL);
   int ret = __syscall_3(SYS_dup3, host_fd, newfd, flags);
   if (ret >= 0) {
      del_guest_fd(vcpu, ret);
      ret = km_add_guest_fd(vcpu, ret, name, flags, ops);
   }
   km_infox(KM_TRACE_FILESYS, "dup3(%d, %d, 0x%x) - %d", fd, newfd, flags, ret);
   return ret;
}

// int dup2(int oldfd, int newfd);
uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd)
{
   if (fd == newfd) {
      return fd;
   }
   return km_fs_dup3(vcpu, fd, newfd, 0);
}

// int pipe(int pipefd[2]);
uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   int host_pipefd[2];
   int ret = __syscall_1(SYS_pipe, (uintptr_t)host_pipefd);
   if (ret == 0) {
      pipefd[0] =
          km_add_guest_fd_internal(vcpu, host_pipefd[0], NULL, O_RDONLY, KM_FILE_HOW_PIPE_0, NULL);
      pipefd[1] =
          km_add_guest_fd_internal(vcpu, host_pipefd[1], NULL, O_WRONLY, KM_FILE_HOW_PIPE_1, NULL);
      km_connect_files(vcpu, pipefd);
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int host_pipefd[2];
   int ret = __syscall_2(SYS_pipe2, (uintptr_t)host_pipefd, flags);
   if (ret == 0) {
      pipefd[0] =
          km_add_guest_fd_internal(vcpu, host_pipefd[0], NULL, flags | O_RDONLY, KM_FILE_HOW_PIPE_0, NULL);
      pipefd[1] =
          km_add_guest_fd_internal(vcpu, host_pipefd[1], NULL, flags | O_WRONLY, KM_FILE_HOW_PIPE_1, NULL);
      km_connect_files(vcpu, pipefd);
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      ret = km_add_guest_fd_internal(vcpu, ret, NULL, 0, KM_FILE_HOW_EVENTFD, NULL);
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      ret = km_add_socket_fd(vcpu, ret, NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKET);
   }
   return ret;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_5(SYS_getsockopt, host_sockfd, level, optname, (uintptr_t)optval, (uintptr_t)optlen);
   return ret;
}

// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_5(SYS_setsockopt, host_sockfd, level, optname, (uintptr_t)optval, optlen);
   return ret;
}

// ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
// ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
uint64_t km_fs_sendrecvmsg(km_vcpu_t* vcpu, int scall, int sockfd, struct msghdr* msg, int flag)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   if (scall == SYS_sendmsg) {
      // translate sent file descriptors if any
      for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
         if (cmsg->cmsg_type == SCM_RIGHTS) {
            int guest_fd = *(int*)CMSG_DATA(cmsg);
            int host_fd = km_fs_g2h_fd(guest_fd, NULL);
            *(int*)CMSG_DATA(cmsg) = host_fd;
            km_infox(KM_TRACE_FILESYS, "send guest fd %d as host %d\n", guest_fd, host_fd);
         }
      }
   }
   int ret = __syscall_3(scall, host_sockfd, (uintptr_t)msg, flag);
   if (scall == SYS_recvmsg) {
      // receive file descriptors if any
      for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
         if (cmsg->cmsg_type == SCM_RIGHTS) {
            int host_fd = *(int*)CMSG_DATA(cmsg);
            int guest_fd =
                km_add_guest_fd_internal(vcpu, host_fd, NULL, flag, KM_FILE_HOW_RECVMSG, NULL);
            *(int*)CMSG_DATA(cmsg) = guest_fd;
            km_infox(KM_TRACE_FILESYS, "received host fd %d as guest %d\n", host_fd, guest_fd);
         }
      }
   }
   return ret;
}

// ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
uint64_t km_fs_sendfile(km_vcpu_t* vcpu, int out_fd, int in_fd, off_t* offset, size_t count)
{
   int host_outfd, host_infd;
   km_file_ops_t* ops;
   if ((host_outfd = km_fs_g2h_fd(out_fd, NULL)) < 0) {
      return -EBADF;
   }
   if ((host_infd = km_fs_g2h_fd(in_fd, &ops)) < 0) {
      return -EBADF;
   }
   if (ops != NULL && ops->read_g2h != NULL) {
      km_warnx("bad fd in sendfile");
      return -EINVAL;
   }
   int ret = __syscall_4(SYS_sendfile, host_outfd, host_infd, (uintptr_t)offset, count);
   return ret;
}

// ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, loff_t *off_out, size_t len,
// unsigned int flags);
uint64_t km_fs_copy_file_range(
    km_vcpu_t* vcpu, int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags)
{
   int host_outfd, host_infd;
   km_file_ops_t* ops;
   if ((host_outfd = km_fs_g2h_fd(fd_out, NULL)) < 0) {
      return -EBADF;
   }
   if ((host_infd = km_fs_g2h_fd(fd_in, &ops)) < 0) {
      return -EBADF;
   }
   if (ops != NULL && ops->read_g2h != NULL) {
      km_warnx("bad fd in copyfilerange");
      return -EINVAL;
   }
   int ret = __syscall_6(SYS_copy_file_range,
                         host_infd,
                         (uintptr_t)off_in,
                         host_outfd,
                         (uintptr_t)off_out,
                         len,
                         flags);
   return ret;
}

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t
km_fs_get_sock_peer_name(km_vcpu_t* vcpu, int hc, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(hc, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_bind, host_sockfd, (uintptr_t)addr, addrlen);
   if (ret == 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->addrlen = addrlen;
      memcpy(sock->addr, addr, addrlen);
      sock->state = KM_SOCK_STATE_BIND;
   }
   return ret;
}

// int listen(int sockfd, int backlog)
uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_listen, host_sockfd, backlog);
   if (ret == 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->state = KM_SOCK_STATE_LISTEN;
      sock->backlog = backlog;
   }
   return ret;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int hostfd = __syscall_3(SYS_accept, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (hostfd < 0) {
      return hostfd;
   }
   km_fd_socket_t* lsock = km_fs()->guest_files[sockfd].sockinfo;
   int guestfd =
       km_add_socket_fd(vcpu, hostfd, NULL, 0, lsock->domain, lsock->type, lsock->protocol, KM_FILE_HOW_ACCEPT);
   if (guestfd < 0) {
      close(hostfd);
      return -ENOMEM;
   }
   km_fd_socket_t* sock = km_fs()->guest_files[guestfd].sockinfo;
   sock->state = KM_SOCK_STATE_ACCEPT;
   return guestfd;
}

// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_connect(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_connect, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->state = KM_SOCK_STATE_CONNECT;
   }
   return ret;
}

// int socketpair(int domain, int type, int protocol, int sv[2]);
uint64_t km_fs_socketpair(km_vcpu_t* vcpu, int domain, int type, int protocol, int sv[2])
{
   int host_sv[2];
   int ret = __syscall_4(SYS_socketpair, domain, type, protocol, (uintptr_t)host_sv);
   if (ret == 0) {
      sv[0] =
          km_add_socket_fd(vcpu, host_sv[0], NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKETPAIR0);
      sv[1] =
          km_add_socket_fd(vcpu, host_sv[1], NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKETPAIR1);
      km_connect_files(vcpu, sv);
   }
   return ret;
}

// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_accept4, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen, flags);
   if (ret >= 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      ret = km_add_socket_fd(vcpu, ret, NULL, 0, sock->domain, sock->type, sock->protocol, KM_FILE_HOW_ACCEPT);
   }
   return ret;
}

// ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                const struct sockaddr *dest_addr, socklen_t addrlen);
uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                      int sockfd,
                      const void* buf,
                      size_t len,
                      int flags,
                      const struct sockaddr* addr,
                      socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_6(SYS_sendto, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, addrlen);
   return ret;
}

// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
uint64_t km_fs_recvfrom(
    km_vcpu_t* vcpu, int sockfd, void* buf, size_t len, int flags, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret =
       __syscall_6(SYS_recvfrom, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

//  int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
uint64_t km_fs_select(km_vcpu_t* vcpu,
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
   int host_nfds = -1;

   if (readfds != NULL) {
      FD_ZERO(&host_readfds_);
      host_readfds = &host_readfds_;
   }
   if (writefds != NULL) {
      FD_ZERO(&host_writefds_);
      host_writefds = &host_writefds_;
   }
   if (exceptfds != NULL) {
      FD_ZERO(&host_exceptfds_);
      host_exceptfds = &host_exceptfds_;
   }

   for (int i = 0; i < nfds; i++) {
      int host_fd = km_fs_g2h_fd(i, NULL);
      if (host_fd < 0) {
         continue;
      }
      if (readfds != NULL && FD_ISSET(i, readfds)) {
         FD_SET(host_fd, host_readfds);
         host_nfds = MAX(host_nfds, host_fd);
      }
      if (writefds != NULL && FD_ISSET(i, writefds)) {
         FD_SET(host_fd, host_writefds);
         host_nfds = MAX(host_nfds, host_fd);
      }
      if (exceptfds != NULL && FD_ISSET(i, exceptfds)) {
         FD_SET(host_fd, host_exceptfds);
         host_nfds = MAX(host_nfds, host_fd);
      }
   }
   host_nfds++;   // per select(2) nfds is highest-numbered file descriptor in any of the three sets, plus 1
   int ret = __syscall_5(SYS_select,
                         host_nfds,
                         (uintptr_t)host_readfds,
                         (uintptr_t)host_writefds,
                         (uintptr_t)host_exceptfds,
                         (uintptr_t)timeout);

   if (ret > 0) {
      if (readfds != NULL) {
         FD_ZERO(readfds);
      }
      if (writefds != NULL) {
         FD_ZERO(writefds);
      }
      if (exceptfds != NULL) {
         FD_ZERO(exceptfds);
      }
      for (int i = 0; i < host_nfds; i++) {
         int guest_fd = km_fs_h2g_fd(i);
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
uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   struct pollfd host_fds[nfds];

   // fds checked before this is called.
   for (int i = 0; i < nfds; i++) {
      if ((host_fds[i].fd = km_fs_g2h_fd(fds[i].fd, NULL)) < 0) {
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

/*
 * epoll handling
 */
static inline km_fs_event_t* km_fs_event_find(km_vcpu_t* vcpu, km_file_t* file, int fd)
{
   km_fs_event_t* event;
   TAILQ_FOREACH (event, &file->events, link) {
      if (event->fd == fd) {
         return event;
      }
   }
   return NULL;
}

static inline void
km_fs_event_add(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   assert(km_fs_event_find(vcpu, file, guestfd) == NULL);
   km_fs_event_t eval = {.fd = guestfd, .event = *event};
   km_fs_event_t* fevent = (km_fs_event_t*)calloc(1, sizeof(km_fs_event_t));
   *fevent = eval;
   TAILQ_INSERT_TAIL(&file->events, fevent, link);
}

static inline void
km_fs_event_mod(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   km_fs_event_t* fevent = km_fs_event_find(vcpu, file, guestfd);
   assert(fevent != NULL);
   fevent->event = *event;
}

static inline void
km_fs_event_del(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   km_fs_event_t* fevent = km_fs_event_find(vcpu, file, guestfd);
   assert(fevent != NULL);
   TAILQ_REMOVE(&file->events, fevent, link);
   free(fevent);
}

// int epoll_create1(int flags);
uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   int ret = -1;
   int hostfd = __syscall_1(SYS_epoll_create1, flags);
   if (hostfd >= 0) {
      ret = km_add_guest_fd_internal(vcpu, hostfd, NULL, 0, KM_FILE_HOW_EVENTFD, NULL);
   }
   return ret;
}

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   int host_epfd;
   int host_fd;

   if ((host_epfd = km_fs_g2h_fd(epfd, NULL)) < 0 || (host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_epoll_ctl, host_epfd, op, host_fd, (uintptr_t)event);
   if (ret == 0) {
      km_file_t* file = &km_fs()->guest_files[epfd];
      switch (op) {
         case EPOLL_CTL_ADD:
            km_fs_event_add(vcpu, file, fd, event);
            break;
         case EPOLL_CTL_MOD:
            km_fs_event_mod(vcpu, file, fd, event);
            break;
         case EPOLL_CTL_DEL:
            km_fs_event_del(vcpu, file, fd, event);
            break;
      }
   }
   return ret;
}

// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
//  const sigset_t *sigmask);
uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                           int epfd,
                           struct epoll_event* events,
                           int maxevents,
                           int timeout,
                           const sigset_t* sigmask,
                           int sigsetsize)
{
   int host_epfd;
   if ((host_epfd = km_fs_g2h_fd(epfd, NULL)) < 0) {
      return -EBADF;
   }

   int ret = __syscall_6(SYS_epoll_pwait,
                         host_epfd,
                         (uintptr_t)events,
                         maxevents,
                         timeout,
                         (uintptr_t)sigmask,
                         sigsetsize);
   return ret;
}

// int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);
uint64_t km_fs_prlimit64(km_vcpu_t* vcpu,
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
   struct rlimit tmp;
   if (resource == RLIMIT_NOFILE && new_limit != NULL) {
      if (new_limit->rlim_cur > km_fs()->nfdmap) {
         return -EPERM;
      }
      tmp.rlim_cur += MAX_KM_FILES;
      tmp = *new_limit;
      new_limit = &tmp;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   if (resource == RLIMIT_NOFILE && old_limit != NULL) {
      old_limit->rlim_cur -= MAX_KM_FILES;
   }
   return ret;
}

size_t km_fs_core_notes_length()
{
   size_t ret = 0;
   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (file->inuse != 0) {
         if (file->how == KM_FILE_HOW_EVENTFD) {
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_eventfd_t);
            km_fs_event_t* ptr;
            TAILQ_FOREACH (ptr, &file->events, link) {
               ret += sizeof(km_nt_event_t);
            }
         } else if (file->sockinfo == NULL) {
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_file_t) +
                   km_nt_file_padded_size(file->name);
         } else {
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_socket_t) +
                   km_nt_file_padded_size(file->name);
         }
      }
   }
   return ret;
}

static inline size_t fs_core_write_nonsocket(char* buf, size_t length, km_file_t* file, int fd)
{
   struct stat st = {};
   if (fstat(fd, &st) < 0) {
      km_warn("fstat failed - ignore");
   }

   char* cur = buf;
   size_t remain = length;
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_FILE,
                             sizeof(km_nt_file_t) + km_nt_file_padded_size(file->name));
   km_nt_file_t* fnote = (km_nt_file_t*)cur;
   cur += sizeof(km_nt_file_t);
   fnote->size = sizeof(km_nt_file_t);
   fnote->fd = fd;
   fnote->flags = file->flags;
   fnote->mode = st.st_mode;
   if (fnote->mode & S_IFIFO) {
      fnote->data = file->ofd;   // default to ofd. override based on file type
   } else {
      fnote->data = lseek(fd, 0, SEEK_CUR);
   }

   strcpy(cur, file->name);
   cur += km_nt_file_padded_size(file->name);

   return cur - buf;
}

static inline size_t fs_core_write_socket(char* buf, size_t length, km_file_t* file, int fd)
{
   char* cur = buf;
   size_t remain = length;
   assert(file->sockinfo != NULL);
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_SOCKET,
                             sizeof(km_nt_socket_t) + roundup(file->sockinfo->addrlen, 4));
   km_nt_socket_t* fnote = (km_nt_socket_t*)cur;
   cur += sizeof(km_nt_socket_t);
   fnote->size = sizeof(km_nt_socket_t);
   fnote->fd = fd;
   fnote->domain = file->sockinfo->domain;
   fnote->type = file->sockinfo->type;
   fnote->protocol = file->sockinfo->protocol;
   fnote->how = file->how;
   fnote->other = file->ofd;
   fnote->addrlen = file->sockinfo->addrlen;
   if (file->sockinfo->addrlen > 0) {
      memcpy(cur, file->sockinfo->addr, file->sockinfo->addrlen);
   }
   fnote->state = KM_NT_SKSTATE_OPEN;
   if (file->sockinfo->state == KM_SOCK_STATE_BIND) {
      fnote->state = KM_NT_SKSTATE_BIND;
   } else if (file->sockinfo->state == KM_SOCK_STATE_LISTEN) {
      fnote->state = KM_NT_SKSTATE_LISTEN;
   } else if (file->sockinfo->state != KM_SOCK_STATE_OPEN) {
      km_warnx("Socket state not OPEN fd=%d state=%d", fd, file->sockinfo->state);
   }
   fnote->backlog = file->sockinfo->backlog;
   cur += roundup(file->sockinfo->addrlen, 4);
   km_infox(KM_TRACE_SNAPSHOT,
            "fd:%d ISOCK: how:=%d %s other=0x%x %d",
            fnote->fd,
            fnote->how,
            file->name,
            fnote->other,
            file->ofd);
   return cur - buf;
}

static inline size_t fs_core_write_eventfd(char* buf, size_t length, km_file_t* file, int fd)
{
   char* cur = buf;
   size_t remain = length;
   km_fs_event_t* event;
   int nevent = 0;

   TAILQ_FOREACH (event, &file->events, link) {
      nevent++;
   }

   km_infox(KM_TRACE_SNAPSHOT, "fd=%d %s nevent=%d", fd, file->name, nevent);

   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_EVENTFD,
                             sizeof(km_nt_eventfd_t) + nevent * sizeof(km_nt_event_t));
   km_nt_eventfd_t fval = {
       .size = sizeof(km_nt_eventfd_t),
       .fd = fd,
       .flags = file->flags,
       .event_size = sizeof(km_nt_event_t),
       .nevent = nevent,

   };
   km_nt_eventfd_t* fnote = (km_nt_eventfd_t*)cur;
   *fnote = fval;
   cur += sizeof(km_nt_eventfd_t);
   TAILQ_FOREACH (event, &file->events, link) {
      km_infox(KM_TRACE_SNAPSHOT, "  monitored event: fd=%d events=0x%x", event->fd, event->event.events);
      km_nt_event_t eval = {.fd = event->fd, .event = event->event.events, .data = event->event.data.u64};
      km_nt_event_t* nt_event = (km_nt_event_t*)cur;
      *nt_event = eval;
      cur += sizeof(km_nt_event_t);
   }

   return cur - buf;
}

size_t km_fs_core_notes_write(char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (file->inuse != 0) {
         size_t sz = 0;
         if (file->how == KM_FILE_HOW_EVENTFD) {
            sz = fs_core_write_eventfd(cur, remain, file, i);
         } else if (file->sockinfo == NULL) {
            sz = fs_core_write_nonsocket(cur, remain, file, i);
         } else {
            sz = fs_core_write_socket(cur, remain, file, i);
         }
         cur += sz;
      }
   }
   return cur - buf;
}

static inline void km_fs_recover_fd(int guestfd, int hostfd, int flags, char* name, int ofd, int how)
{
   km_file_t* file = &km_fs()->guest_files[guestfd];

   if (guestfd != hostfd) {
      if (guestfd != dup2(hostfd, guestfd)) {
         km_warn("can not dup2 %s to %d", name, guestfd);
         pause();
         return;
      }
      close(hostfd);
   }

   file->inuse = 1;
   file->how = how;
   file->flags = flags;
   file->name = strdup(name);
   file->ofd = ofd;
   TAILQ_INIT(&file->events);
   km_infox(KM_TRACE_SNAPSHOT,
            "guestfd=%d hostfd=%d flags=0x%x name=%s ofd=%d how=%d",
            guestfd,
            hostfd,
            flags,
            name,
            ofd,
            how);
}

static inline int km_fs_recover_pipe(km_nt_file_t* nt_file, char* name)
{
   km_file_t* file = &km_fs()->guest_files[nt_file->fd];
   if (file->inuse != 0) {
      // Filled in by other side
      assert(file->ofd == nt_file->data);
      return 0;
   }
   int hostfd[2];
   int syscall_flags = nt_file->flags & ~O_WRONLY;
   if (pipe2(hostfd, syscall_flags) < 0) {
      km_warn("pip recover failure");
      return -1;
   }
   /*
    * pipes are asymmetric. Make sure restored correctly.
    */
   int rfd = nt_file->fd;
   int wfd = nt_file->data;
   if ((nt_file->flags & O_WRONLY) != 0) {
      rfd = nt_file->data;
      wfd = nt_file->fd;
   }
   if (rfd >= 0) {
      km_fs_recover_fd(rfd, hostfd[0], syscall_flags, km_get_nonfile_name(hostfd[0]), wfd, nt_file->how);
   }
   if (wfd >= 0) {
      km_fs_recover_fd(wfd, hostfd[1], syscall_flags, km_get_nonfile_name(hostfd[1]), rfd, nt_file->how);
   }
   km_infox(KM_TRACE_SNAPSHOT, "recovered pipe: rfd=%d wfd=%d flags=0x%x", rfd, wfd, syscall_flags);
   return 0;
}

static inline int km_fs_recover_socket(km_nt_socket_t* nt_sock, struct sockaddr* addr, int addrlen)
{
   km_file_t* file = &km_fs()->guest_files[nt_sock->fd];
   assert(file->inuse == 0);

   int host_fd = socket(nt_sock->domain, nt_sock->type, nt_sock->protocol);
   if (host_fd < 0) {
      km_warn("socket recover");
      return -1;
   }
   km_fs_recover_fd(nt_sock->fd, host_fd, 0, km_get_nonfile_name(host_fd), -1, nt_sock->how);

   km_fd_socket_t sval = {
       .domain = nt_sock->domain,
       .type = nt_sock->type,
       .protocol = nt_sock->protocol,
   };
   km_fd_socket_t* sockinfo = (km_fd_socket_t*)malloc(sizeof(km_fd_socket_t));
   *sockinfo = sval;
   if (addrlen > 0) {
      sockinfo->addrlen = addrlen;
      memcpy(sockinfo->addr, addr, addrlen);
   }
   file->sockinfo = sockinfo;

   return 0;
}

static int km_fs_recover_open_file(char* ptr, size_t length)
{
   km_nt_file_t* nt_file = (km_nt_file_t*)ptr;
   if (nt_file->size != sizeof(km_nt_file_t)) {
      km_warnx("nt_km_file_t size mismatch - old snapshot?");
      return -1;
   }
   char* name = ptr + sizeof(km_nt_file_t);
   km_infox(KM_TRACE_SNAPSHOT,
            "fd=%d name=%s flags=0x%x mode=%o pos=%ld",
            nt_file->fd,
            name,
            nt_file->flags,
            nt_file->mode,
            nt_file->data);

   /*
    * If the std fds names are [std{in,out,err}] (as set in km_fs_init()) we inherit the fds from km,
    * otherwise process them in a regular way. Note the dup2 in below will close the km inherited fd.
    */
   if ((nt_file->fd == 0 && strcmp(name, stdin_name) == 0) ||
       (nt_file->fd == 1 && strcmp(name, stdout_name) == 0) ||
       (nt_file->fd == 2 && strcmp(name, stderr_name) == 0)) {
      return 0;
   }

   if (nt_file->fd < 0 || nt_file->fd >= km_fs()->nfdmap) {
      km_warnx("bad file descriptor=%d", nt_file->fd);
      return -1;
   }

   if ((nt_file->mode & __S_IFMT) == __S_IFSOCK) {
      km_warnx("TODO: recover __S_ISOCK data=0x%lx", nt_file->data);
      return -1;
   }
   if ((nt_file->mode & __S_IFMT) == __S_IFIFO) {
      return km_fs_recover_pipe(nt_file, name);
   }
   if ((nt_file->mode & __S_IFMT) == 0) {
      km_warnx("TODO: recover epollfd");
      return 0;
   }

   int fd = open(name, nt_file->flags, 0);
   if (fd < 0) {
      km_warn("cannon open %s", name);
      return -1;
   }

   struct stat st;
   if (fstat(fd, &st) < 0) {
      km_warn("cannon fstat %s", name);
      return -1;
   }
   if (st.st_mode != nt_file->mode) {
      km_warnx("file mode mistmatch expect %o got %o %s", nt_file->mode, st.st_mode, name);
      return -1;
   }

   km_file_t* file = &km_fs()->guest_files[nt_file->fd];
   file->inuse = 1;
   file->flags = nt_file->flags;
   file->name = strdup(name);

   km_fs_recover_fd(nt_file->fd, fd, nt_file->flags, name, nt_file->data, nt_file->how);
   if ((nt_file->mode & __S_IFMT) == __S_IFREG && nt_file->data != 0) {
      if (lseek(nt_file->fd, nt_file->data, SEEK_SET) != nt_file->data) {
         km_warn("lseek failed");
         return -1;
      }
   }
   return 0;
}

// return filename to open for the request for /proc/`getpid()`/sched
static int proc_sched_open(const char* guest_fn, char* host_fn, size_t host_fn_sz)
{
   return snprintf(host_fn, host_fn_sz, "%s%s", PROC_SELF, guest_fn + proc_pid_length);
}

static inline int km_vcpu_count_running(km_vcpu_t* vcpu, uint64_t unused)
{
   return vcpu->is_active;
}

// called on the read of /proc/self/sched - need to replace the first line
static int proc_sched_read(int fd, char* buf, size_t buf_sz)
{
   fd = dup(fd);
   FILE* fp = fdopen(fd, "r");
   if (fp == NULL) {
      if (fd >= 0) {
         close(fd);
      }
      return -errno;
   }
   char tmp[128];
   fgets(tmp, sizeof(tmp), fp);   // skip the first line
   if (feof(fp)) {                // second read, to make sure we are at end of file
      fclose(fp);
      return 0;
   }
   int ret = snprintf(buf,
                      buf_sz,
                      "%s (%u, #threads: %u)\n",
                      km_guest.km_filename,
                      machine.pid,
                      km_vcpu_apply_all(km_vcpu_count_running, 0));
   ret += fread(buf + ret, 1, buf_sz - ret, fp);
   fclose(fp);
   return ret;
}

// called on the open of /proc/pid/cmdline
static int proc_cmdline_open(const char* name, char* buf, size_t bufsz)
{
   return snprintf(buf, bufsz, "%s%s", PROC_SELF, name + proc_pid_length);
}

static int proc_self_getdents(int fd, /* struct linux_dirent64* */ void* buf, size_t buf_sz)
{
   struct linux_dirent64 {
      ino64_t d_ino;           /* 64-bit inode number */
      off64_t d_off;           /* 64-bit offset to next structure */
      unsigned short d_reclen; /* Size of this dirent */
      unsigned char d_type;    /* File type */
      char d_name[];           /* Filename (null-terminated) */
   };
   int ret = __syscall_3(SYS_getdents64, fd, (uint64_t)buf, buf_sz);
   struct linux_dirent64* e;
   for (off64_t offset = 0; offset < ret; offset += e->d_reclen) {
      e = (struct linux_dirent64*)(buf + offset);
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
         continue;
      }
      ino64_t ino;
      if (sscanf(e->d_name, "%lu", &ino) != 1 || ino >= MAX_OPEN_FILES - MAX_KM_FILES) {
         return offset;
      }
   }
   return ret;
}

/*
 * Table of pathnames to watch for and process specially. The patterns are processed by
 * km_fs_filename_init(), first by applying PID for /proc/%u pattern, then compile the pattern into
 * regex, to match pathname on filepaths ops like open, stat, readlink...
 * Regular files won't match, all of the file system ops are regular. If the name matches, some of
 * the ops might need to be done specially, eg /proc/self/sched content needs to be modified for
 * read, or /proc/self/fd getdents. ops is vector of these ops as well as name matching for open and
 * readlink. If name matches on open the returned ops is stored in fd translation structure
 * (km_file_t). When guest to host fd ctranslation is done the ops is also returned, and function
 * pointers used to alter the functionality of file system ops.
 */
static km_filename_table_t km_filename_table[] = {
    {
        .pattern = "^/proc/self/fd/[[:digit:]]+$",
        .ops = {.open_g2h = proc_self_fd_name, .readlink_g2h = proc_self_fd_name},
    },
    {
        .pattern = "^/proc/self/exe$",
        .ops = {.open_g2h = proc_self_exe_name, .readlink_g2h = proc_self_exe_name},
    },
    {
        .pattern = "^/proc/self/fd$",
        .ops = {.getdents_g2h = proc_self_getdents},
    },
    {
        .pattern = "^/proc/self/sched$",
        .ops = {.read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/%u/fd/[[:digit:]]+$",
        .ops = {.open_g2h = proc_self_fd_name, .readlink_g2h = proc_self_fd_name},
    },
    {
        .pattern = "^/proc/%u/exe$",
        .ops = {.open_g2h = proc_self_exe_name, .readlink_g2h = proc_self_exe_name},
    },
    {
        .pattern = "^/proc/%u/fd$",
        .ops = {.getdents_g2h = proc_self_getdents},
    },
    {
        .pattern = "^/proc/%u/sched$",
        .ops = {.open_g2h = proc_sched_open, .read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/%u/cmdline$",
        .ops = {.open_g2h = proc_cmdline_open},
    },
    {},
};

// Given pointer to ops field from the table above, return line # in that table, or -1

#define container_of(ptr, type, member) ((type*)((char*)(ptr) - __builtin_offsetof(type, member)))
int km_filename_table_line(km_file_ops_t* o)
{
   if (o == NULL) {
      return -1;
   }
   return container_of(o, km_filename_table_t, ops) - km_filename_table;
}

// given line # retreive ops
km_file_ops_t* km_file_ops(int i)
{
   if (i < 0) {
      return NULL;
   }
   return &km_filename_table[i].ops;
}

/*
 * For now every entry in the table starts with "/proc". Most files wont match, so we check for
 * "/proc" first.
 */
static const_string_t km_filename_prefix = "/proc";
static const int km_filename_prefix_sz = 5;
/*
 * Check and translate if necessary the guest into host view of the file name, for example
 * "/proc/...", as needs to be done for open and friends (stat ...)
 *
 * Return: 0 if no match
 *        >0 for successful match, length of new name
 *        <0 (-errno) for match that shouldn't open
 *
 * Also return ops vector associated with the new fd to be used in subsequent file ops. For
 * instance, read_g2h() would massage the returned buffer for something like /proc/self/sched
 */
static int km_fs_g2h_filename(const char* name, char* buf, size_t bufsz, km_file_ops_t** ops)
{
   if (strncmp(km_filename_prefix, name, km_filename_prefix_sz) != 0) {
      if (ops != NULL) {
         *ops = NULL;
      }
      return 0;
   }
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (regexec(&t->regex, name, 0, NULL, 0) == 0) {
         if (ops != NULL) {
            *ops = &t->ops;
         }
         if (t->ops.open_g2h != NULL) {
            return t->ops.open_g2h(name, buf, bufsz);
         }
         strncpy(buf, name, bufsz);
         return 1;
      }
   }
   // no match, regular file
   if (ops != NULL) {
      *ops = NULL;
   }
   return 0;
}

/*
 * Check and translate if necessary the guest into host view of the file name, for example
 * "/proc/...", for readlinks
 * Return: 0 if no match
 *        <0 (-errno) for match that shouldn't work
 */
static int km_fs_g2h_readlink(const char* name, char* buf, size_t bufsz)
{
   if (strncmp(km_filename_prefix, name, km_filename_prefix_sz) != 0) {
      return 0;
   }
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (regexec(&t->regex, name, 0, NULL, 0) == 0) {
         if (t->ops.readlink_g2h == NULL) {
            return -ENOENT;
         }
         return t->ops.readlink_g2h(name, buf, bufsz);
      }
   }
   return 0;
}

/*
 * Process the table or filenames to watch for
 */
static void km_fs_filename_init(void)
{
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (strncmp(t->pattern + 1, PROC_PID, strlen(PROC_PID)) == 0) {   // +1 to skip ^
         char pat[128];
         snprintf(pat, sizeof(pat), t->pattern, machine.pid);
         if (regcomp(&t->regex, pat, REG_EXTENDED | REG_NOSUB) != 0) {
            km_err(1, "filename regcomp failed, exiting...");
         }
      } else {
         if (regcomp(&t->regex, t->pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            km_err(1, "filename regcomp failed, exiting...");
         }
      }
   }
   if ((km_my_exec = realpath(PROC_SELF_EXE, NULL)) == NULL) {
      km_err(1, "realpath /proc/self/exe failed");
   }
}

static void km_fs_filename_fini(void)
{
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      regfree(&t->regex);
   }
   free(km_my_exec);
}

int km_fs_init(void)
{
   struct rlimit lim;

   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -errno;
   }

   machine.filesys = calloc(1, sizeof(km_filesys_t));
   lim.rlim_cur = MAX_OPEN_FILES - MAX_KM_FILES;   // Limit max open files. TODO: config

   km_infox(KM_TRACE_FILESYS, "lim.rlim_cur=%ld", lim.rlim_cur);
   km_fs()->nfdmap = lim.rlim_cur;
   km_fs()->guest_files = calloc(lim.rlim_cur, sizeof(km_file_t));
   assert(km_fs()->guest_files != NULL);

   if (km_exec_recover_guestfd() != 0) {
      // parent invocation - setup guest std file streams.
      for (int i = 0; i < 3; i++) {
         km_file_t* file = &km_fs()->guest_files[i];
         assert(file->inuse != 1);
         file->inuse = 1;
         file->ofd = -1;
         switch (i) {
            case 0:
               file->name = strdup("[stdin]");
               file->flags = O_RDONLY;
               break;
            case 1:
               file->name = strdup("[stdout]");
               file->flags = O_WRONLY;
               break;
            case 2:
               file->name = strdup("[stderr]");
               file->flags = O_WRONLY;
               break;
         }
      }
   }
   snprintf(proc_pid_exe, sizeof(proc_pid_exe), PROC_PID_EXE, machine.pid);
   snprintf(proc_pid_fd, sizeof(proc_pid_fd), PROC_PID_FD, machine.pid);
   snprintf(proc_pid, sizeof(proc_pid), PROC_PID, machine.pid);
   proc_pid_length = strlen(proc_pid);
   km_fs_filename_init();
   return 0;
}

void km_fs_fini(void)
{
   km_fs_filename_fini();
   if (km_fs() == NULL) {
      return;
   }
   if (km_fs()->guest_files != NULL) {
      for (int i = 0; i < km_fs()->nfdmap; i++) {
         km_file_t* file = &km_fs()->guest_files[i];
         if (file->name != NULL) {
            free(file->name);
         }
      }
      free(km_fs()->guest_files);
   }
   free(machine.filesys);
}

/*
 * === KM internal fd management.
 */

static int internal_fd = KM_START_FDS;

// dup internal fd to km private area
static int km_internal_fd(int fd, int km_fd)
{
   if (fd < 0) {
      return fd;
   }
   int newfd;
   if (km_fd == -1) {
      newfd = dup2(fd, internal_fd++);
      assert(newfd >= 0 && newfd < MAX_OPEN_FILES);
   } else {
      newfd = dup2(fd, km_fd);
   }
   if (newfd >= MAX_OPEN_FILES - MAX_KM_FILES) {
      close(fd);
   }
   return newfd;
}

int km_internal_open(const char* name, int flag)
{
   int fd = open(name, flag);
   return km_internal_fd(fd, -1);
}

int km_internal_eventfd(unsigned int initval, int flags)
{
   int fd = eventfd(initval, flags);
   return km_internal_fd(fd, -1);
}

int km_internal_fd_ioctl(int fd, unsigned long request, ...)
{
   void* arg;
   va_list ap;
   va_start(ap, request);
   arg = va_arg(ap, void*);
   va_end(ap);

   int retfd = ioctl(fd, request, arg);
   return km_internal_fd(retfd, -1);
}

int km_gdb_listen(int domain, int type, int protocol)
{
   int fd = socket(domain, type, protocol);
   return km_internal_fd(fd, KM_GDB_LISTEN);
}

int km_gdb_accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
   int newfd = accept(fd, addr, addrlen);
   return km_internal_fd(newfd, KM_GDB_ACCEPT);
}

int km_mgt_listen(int domain, int type, int protocol)
{
   int fd = socket(domain, type, protocol);
   return km_internal_fd(fd, KM_MGM_LISTEN);
}

int km_mgt_accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
   int newfd = accept(fd, addr, addrlen);
   return km_internal_fd(newfd, KM_MGM_ACCEPT);
}

/*
 * Use the dup of fd 2 or a new one to open km_log_file in km dedicated fd area,
 * to be used for km logging
 */
void km_redirect_msgs(const char* name)
{
   int fd, fd1;
   if (name != NULL) {
      fd1 = open(name, O_WRONLY | O_CREAT, 0644);
      fd = dup2(fd1, KM_LOGGING);
      close(fd1);
   } else {
      fd = dup2(2, KM_LOGGING);
   }
   assert(fd == KM_LOGGING);

   if ((km_log_file = fdopen(fd, "w")) == NULL) {
      km_err(1, "Failed to redirect km log");
   }
   setlinebuf(km_log_file);
}

/*
 * Close stdin, stdout, and stderr FILE* but keep the file descriptors open for guest use. Guest has
 * its own stdio FILE* inside.
 */
void km_close_stdio(int log_to_fd)
{
   int fdin = dup(0);
   fclose(stdin);
   stdin = NULL;
   dup2(fdin, 0);
   close(fdin);

   int fdout, fderr;
   if (log_to_fd == -1) {
      fdout = dup(1);
      fderr = dup(2);
   } else {
      fdout = log_to_fd;
      fderr = log_to_fd;
   }
   fclose(stdout);
   fclose(stderr);
   stdout = NULL;
   stderr = NULL;
   dup2(fdout, 1);
   dup2(fderr, 2);
   if (log_to_fd == -1) {
      close(fdout);
      close(fderr);
   } else {
      close(log_to_fd);
   }
}

/*
 *   == Snapshot recovery for files
 */

static int km_fs_recover_socketpair(km_nt_socket_t* nt_sock)
{
   km_infox(KM_TRACE_SNAPSHOT,
            "socketpair: fd=%d how=%d other=%d domain=%d type=%d protocol=%d",
            nt_sock->fd,
            nt_sock->how,
            nt_sock->other,
            nt_sock->domain,
            nt_sock->type,
            nt_sock->protocol);

   if (km_fs()->guest_files[nt_sock->fd].inuse != 0) {
      return 0;
   }

   int host_sv[2];
   if (socketpair(nt_sock->domain, nt_sock->type, nt_sock->protocol, host_sv) < 0) {
      km_warn("socketpair recovery failue");
      return -1;
   }

   int otherfd = nt_sock->other;
   if (nt_sock->how == KM_FILE_HOW_SOCKETPAIR0) {
      if (otherfd == host_sv[0]) {
         km_fs_recover_fd(otherfd,
                          host_sv[1],
                          0,
                          km_get_nonfile_name(host_sv[1]),
                          nt_sock->fd,
                          KM_FILE_HOW_SOCKETPAIR1);
         otherfd = -1;
      }
      km_fs_recover_fd(nt_sock->fd,
                       host_sv[0],
                       0,
                       km_get_nonfile_name(host_sv[0]),
                       nt_sock->other,
                       KM_FILE_HOW_SOCKETPAIR0);
      if (otherfd == -1) {
         close(host_sv[1]);
      } else {
         km_fs_recover_fd(otherfd,
                          host_sv[1],
                          0,
                          km_get_nonfile_name(host_sv[1]),
                          nt_sock->fd,
                          KM_FILE_HOW_SOCKETPAIR1);
      }
   } else {
      if (otherfd == host_sv[1]) {
         km_fs_recover_fd(otherfd,
                          host_sv[0],
                          0,
                          km_get_nonfile_name(host_sv[0]),
                          nt_sock->fd,
                          KM_FILE_HOW_SOCKETPAIR0);
         otherfd = -1;
      }
      km_fs_recover_fd(nt_sock->fd,
                       host_sv[1],
                       0,
                       km_get_nonfile_name(host_sv[1]),
                       nt_sock->other,
                       KM_FILE_HOW_SOCKETPAIR1);
      if (otherfd == -1) {
         close(host_sv[0]);
      } else {
         km_fs_recover_fd(otherfd,
                          host_sv[0],
                          0,
                          km_get_nonfile_name(host_sv[0]),
                          nt_sock->fd,
                          KM_FILE_HOW_SOCKETPAIR0);
      }
   }
   return 0;
}

static int km_fs_recover_socket_accepted(km_nt_socket_t* nt_sock)
{
   return 0;
}

static int km_fs_recover_open_socket(char* ptr, size_t length)
{
   km_nt_socket_t* nt_sock = (km_nt_socket_t*)ptr;
   if (nt_sock->size != sizeof(km_nt_socket_t)) {
      km_warnx("nt_km_socket_t size mismatch - old snapshot?");
      return -1;
   }
   if (nt_sock->how == KM_FILE_HOW_SOCKETPAIR0 || nt_sock->how == KM_FILE_HOW_SOCKETPAIR1) {
      return km_fs_recover_socketpair(nt_sock);
   }
   if (nt_sock->how == KM_FILE_HOW_ACCEPT) {
      return km_fs_recover_socket_accepted(nt_sock);
   }

   /*
    * Assume socket optianally bound for listening
    */
   km_infox(KM_TRACE_SNAPSHOT,
            "socket: fd=%d other=%d how=%d addrlen=%d",
            nt_sock->fd,
            nt_sock->other,
            nt_sock->how,
            nt_sock->addrlen);

   struct sockaddr* sa = (struct sockaddr*)(ptr + sizeof(km_nt_socket_t));
   km_fs_recover_socket(nt_sock, sa, nt_sock->addrlen);

   if (nt_sock->state == KM_SOCK_STATE_BIND || nt_sock->state == KM_SOCK_STATE_LISTEN) {
      int hostfd = km_fs_g2h_fd(nt_sock->fd, NULL);
      if (bind(hostfd, sa, nt_sock->addrlen) < 0) {
         km_warn("recover bind failed");
         return -1;
      }
      if (nt_sock->state == KM_SOCK_STATE_LISTEN) {
         if (listen(hostfd, nt_sock->backlog) < 0) {
            km_warn("recover listen failed");
            return -1;
         }
      }
   }

   return 0;
}

static int km_fs_recover_eventfd(char* ptr, size_t length)
{
   char* cur = ptr;
   km_nt_eventfd_t* nt_eventfd = (km_nt_eventfd_t*)cur;
   cur += sizeof(km_nt_eventfd_t);

   km_infox(KM_TRACE_SNAPSHOT, "EVENTFD fd=%d", nt_eventfd->fd);
   if (nt_eventfd->size != sizeof(km_nt_eventfd_t)) {
      km_warnx("nt_km_eventfd_t size mismatch - old snapshot?");
      return -1;
   }

   km_file_t* file = &km_fs()->guest_files[nt_eventfd->fd];
   assert(file->inuse == 0);

   int hostfd = epoll_create1(nt_eventfd->flags);
   if (hostfd < 0) {
      km_warn("epoll_create failed");
      return -1;
   }
   km_fs_recover_fd(nt_eventfd->fd, hostfd, 0, km_get_nonfile_name(hostfd), -1, KM_FILE_HOW_EVENTFD);
   for (int i = 0; i < nt_eventfd->nevent; i++) {
      km_nt_event_t* nt_event = (km_nt_event_t*)cur;
      int host_efd = km_fs_g2h_fd(nt_event->fd, NULL);
      if (host_efd < 0) {
         km_warnx("monitored fd=%d does not exist", nt_event->fd);
         return -1;
      }
      struct epoll_event ev = {.events = nt_event->event, .data.u64 = nt_event->data};
      if (epoll_ctl(nt_eventfd->fd, EPOLL_CTL_ADD, host_efd, &ev) < 0) {
         km_warn("epoll_ctl for fd=%d failed", nt_event->fd);
         return -1;
      }

      km_fs_event_t* event = calloc(1, sizeof(km_fs_event_t));
      event->fd = nt_event->fd;
      event->event.events = nt_event->event;
      event->event.data.u64 = nt_event->data;
      TAILQ_INSERT_TAIL(&file->events, event, link);
      cur += sizeof(km_nt_event_t);
   }

   return 0;
}

int km_fs_recover(char* notebuf, size_t notesize)
{
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_FILE, km_fs_recover_open_file) < 0) {
      km_errx(2, "recover open files failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_SOCKET, km_fs_recover_open_socket) < 0) {
      km_errx(2, "recover open files failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_EVENTFD, km_fs_recover_eventfd) < 0) {
      km_errx(2, "recover open files failed");
   }
   return 0;
}
