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
 * Every file descriptor opened by a guest payload has a coresponding open file
 * descriptor in KM. The the guest's file descriptor table is virtualized and
 * a guest file decriptor number is mapped to a KM process file descriptor number.
 * Likewise host file descriptor numbers are mapped to guest file descriptor
 * numbers when events involving file descriptors need to be forwarded to the
 * guest payload.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
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

#include "km.h"
#include "km_coredump.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_mem.h"
#include "km_snapshot.h"
#include "km_syscall.h"

#define MAX_OPEN_FILES (1024)

static char proc_pid_fd[128];
static char proc_pid_exe[128];
static char proc_pid[128];
static int proc_pid_length;

typedef struct km_file {
   int inuse;
   int guestfd;
   int hostfd;
   int flags;            // Open flags
   km_file_ops_t* ops;   // Overwritten file ops for file matched at open
   char* name;
} km_file_t;

typedef struct km_filesys {
   int nfdmap;                   // size of file descriptor maps
   km_file_t* guest_files;       // Indexed by guestfd
   int* hostfd_to_guestfd_map;   // reverse file descriptor map
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
int km_add_guest_fd(
    km_vcpu_t* vcpu, int host_fd, int start_guestfd, char* name, int flags, km_file_ops_t* ops)
{
   assert(host_fd >= 0 && host_fd < km_fs()->nfdmap);
   assert(start_guestfd >= 0 && start_guestfd < km_fs()->nfdmap);
   int guest_fd = -1;
   for (int i = start_guestfd; i < km_fs()->nfdmap; i++) {
      int available = 0;
      int taken = 1;
      if (__atomic_compare_exchange_n(&km_fs()->guest_files[i].inuse,
                                      &available,
                                      taken,
                                      0,
                                      __ATOMIC_SEQ_CST,
                                      __ATOMIC_SEQ_CST) != 0) {
         km_file_t* file = &km_fs()->guest_files[i];
         file->guestfd = i;
         file->hostfd = host_fd;
         file->ops = ops;
         if (name == NULL) {
            file->name = km_get_nonfile_name(host_fd);
         } else {
            file->name = strdup(name);
         }
         file->flags = flags;

         __atomic_store_n(&km_fs()->hostfd_to_guestfd_map[host_fd], i, __ATOMIC_SEQ_CST);
         void* newval = NULL;
         if (name != NULL) {
            newval = strdup(name);
            assert(newval != NULL);
         }
         guest_fd = i;
         break;
      }
   }
   if (guest_fd < 0) {
      err(2, "%s: no space to add guestfd", __FUNCTION__);
   }
   return guest_fd;
}

/*
 * deletes an exist guestfd to hostfd mapping (used by km_fs_close())
 */
static inline void del_guest_fd(km_vcpu_t* vcpu, int guestfd, int hostfd)
{
   assert(hostfd >= 0 && hostfd < km_fs()->nfdmap);
   int rc = __atomic_compare_exchange_n(&km_fs()->hostfd_to_guestfd_map[hostfd],
                                        &guestfd,
                                        -1,
                                        0,
                                        __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST);
   assert(rc != 0);

   assert(guestfd >= 0 && guestfd < km_fs()->nfdmap);
   km_file_t* file = &km_fs()->guest_files[guestfd];
   int allocated = 1;
   rc = __atomic_compare_exchange_n(&file->inuse, &allocated, 0, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

   assert(rc != 0);
   if (file->name != NULL) {
      free(file->name);
      file->name = NULL;
   }
}

char* km_guestfd_name(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= km_fs()->nfdmap) {
      return NULL;
   }
   return km_fs()->guest_files[fd].name;
}

/*
 * Replaces the mapping for a guest file descriptor. Used by dup2(2) and dup(3).
 */
static int replace_guest_fd(km_vcpu_t* vcpu, int guest_fd, int host_fd, int flags, km_file_ops_t* ops)
{
   assert(guest_fd >= 0 && guest_fd < km_fs()->nfdmap);
   assert(host_fd >= 0 && host_fd < km_fs()->nfdmap);
   km_file_t* file = &km_fs()->guest_files[guest_fd];
   int was_inuse = __atomic_exchange_n(&file->inuse, 1, __ATOMIC_SEQ_CST);
   __atomic_store_n(&file->guestfd, guest_fd, __ATOMIC_SEQ_CST);
   file->flags = flags;
   file->ops = ops;
   int close_fd = __atomic_exchange_n(&file->hostfd, host_fd, __ATOMIC_SEQ_CST);
   __atomic_store_n(&km_fs()->hostfd_to_guestfd_map[host_fd], guest_fd, __ATOMIC_SEQ_CST);
   if (was_inuse != 0) {
      /*
       * stdin, stdout, and stderr are shared with KM,
       * we don't want to close them.
       * TODO(maybe): If there is more than one cycle of this on
       * std fd,
       */
      if (close_fd > 2) {
         __atomic_store_n(&km_fs()->hostfd_to_guestfd_map[close_fd], -1, __ATOMIC_SEQ_CST);
         __syscall_1(SYS_close, close_fd);
      }
   }
   return guest_fd;
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
   int guest_fd = __atomic_load_n(&km_fs()->hostfd_to_guestfd_map[hostfd], __ATOMIC_SEQ_CST);
   if (__atomic_load_n(&km_fs()->guest_files[guest_fd].hostfd, __ATOMIC_SEQ_CST) != hostfd) {
      guest_fd = -ENOENT;
   }
   return guest_fd;
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
   int ret = __atomic_load_n(&km_fs()->guest_files[fd].hostfd, __ATOMIC_SEQ_CST);
   assert((ret == -1) || (km_fs()->hostfd_to_guestfd_map[ret] == fd) ||
          (km_fs()->hostfd_to_guestfd_map[ret] == -1));
   if (ops != NULL) {
      *ops = km_fs()->guest_files[fd].ops;
   }
   return ret;
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
      guestfd = km_add_guest_fd(vcpu, hostfd, 0, pathname, flags, ops);
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
         km_err_msg(0, "bad dirfd in openat");
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
      guestfd = km_add_guest_fd(vcpu, hostfd, 0, pathname, flags, ops);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "openat(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

// int close(fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   km_infox(KM_TRACE_FILESYS, "close(%d)", fd);
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
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
   del_guest_fd(vcpu, fd, host_fd);
   int ret = 0;
   // KM guest can't close host's stdin, stdout, and stderr.
   if (host_fd > 2) {
      ret = __syscall_1(SYS_close, host_fd);
      if (ret != 0) {
         warn("close of guest fd %d (hostfd %d) returned an error: %d", fd, host_fd, ret);
      }
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
         km_err_msg(0, "unsupported %s", km_hc_name_get(scall));
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
      km_err_msg(0, "unsupported %s", km_hc_name_get(scall));
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
      farg = 0;
   }
   int ret = __syscall_3(SYS_fcntl, host_fd, cmd, farg);
   if ((cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) && ret >= 0) {
      ret =
          km_add_guest_fd(vcpu, ret, arg, km_guestfd_name(vcpu, fd), (cmd == F_DUPFD) ? 0 : O_CLOEXEC, ops);
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
      km_err_msg(0, "unsupported lseek on %s", km_guestfd_name(vcpu, fd));
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
      km_err_msg(0, "bad linkpath %s in symlink", linkpath);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(target, NULL, 0, NULL);
   if (ret != 0) {
      km_err_msg(0, "bad target %s in symlink", target);
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
      km_err_msg(0, "bad oldpath %s in rename", old);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(new, NULL, 0, NULL);
   if (ret != 0) {
      km_err_msg(0, "bad new %s in rename", new);
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
   if (fd < 0 || fd >= km_fs()->nfdmap || (mpath = km_fs()->guest_files[fd].name) == 0) {
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
   if (strcmp(pathname, PROC_SELF_EXE) != 0 && strcmp(pathname, proc_pid_exe) != 0) {
      return -ENOENT;
   }
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
      ret = __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
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
            km_err_msg(0, "bad dirfd in readlinkat");
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
      km_err_msg(0, "bad pathname %s in chdir", pathname);
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
      km_err_msg(0, "bad fd in fchdir");
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
         km_err_msg(0, "bad dirfd in statx");
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
      km_infox(KM_TRACE_FILESYS, "%s guest fd %d", file_name, fd);
   }

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
      ret = km_add_guest_fd(vcpu, ret, 0, name, 0, ops);
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
   int ret = __syscall_1(SYS_dup, host_fd);
   if (ret >= 0) {
      ret = replace_guest_fd(vcpu, newfd, ret, flags, ops);
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
      pipefd[0] = km_add_guest_fd(vcpu, host_pipefd[0], 0, NULL, 0, NULL);
      pipefd[1] = km_add_guest_fd(vcpu, host_pipefd[1], 0, NULL, 0, NULL);
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int host_pipefd[2];
   int ret = __syscall_2(SYS_pipe2, (uintptr_t)host_pipefd, flags);
   if (ret == 0) {
      pipefd[0] = km_add_guest_fd(vcpu, host_pipefd[0], 1, NULL, 0, NULL);
      pipefd[1] = km_add_guest_fd(vcpu, host_pipefd[1], 0, NULL, 0, NULL);
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      ret = km_add_guest_fd(vcpu, ret, 0, NULL, 0, NULL);
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      ret = km_add_guest_fd(vcpu, ret, 0, NULL, 0, NULL);
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
            int guest_fd = km_add_guest_fd(vcpu, host_fd, 0, NULL, flag, NULL);
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
      km_err_msg(0, "bad fd in sendfile");
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
      km_err_msg(0, "bad fd in copyfilerange");
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
   return ret;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_accept, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      ret = km_add_guest_fd(vcpu, ret, 0, NULL, 0, NULL);
   }
   return ret;
}

// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_connect(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_connect, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

// int socketpair(int domain, int type, int protocol, int sv[2]);
uint64_t km_fs_socketpair(km_vcpu_t* vcpu, int domain, int type, int protocol, int sv[2])
{
   int ret = __syscall_4(SYS_socketpair, domain, type, protocol, (uintptr_t)sv);
   if (ret == 0) {
      sv[0] = km_add_guest_fd(vcpu, sv[0], 0, NULL, 0, NULL);
      sv[1] = km_add_guest_fd(vcpu, sv[1], 0, NULL, 0, NULL);
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
      ret = km_add_guest_fd(vcpu, ret, 0, NULL, 0, NULL);
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

// int epoll_create1(int flags);
uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   int ret = __syscall_1(SYS_epoll_create1, flags);
   if (ret >= 0) {
      ret = km_add_guest_fd(vcpu, ret, 0, NULL, 0, NULL);
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
   if (resource == RLIMIT_NOFILE && new_limit != NULL && new_limit->rlim_cur > km_fs()->nfdmap) {
      return -EPERM;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   return ret;
}

size_t km_fs_core_notes_length()
{
   size_t ret = 0;
   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (file->inuse != 0) {
         ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_file_t) +
                km_nt_file_padded_size(file->name);
      }
   }
   return ret;
}

size_t km_fs_core_notes_write(char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (file->inuse != 0) {
         cur += km_add_note_header(cur,
                                   remain,
                                   KM_NT_NAME,
                                   NT_KM_FILE,
                                   sizeof(km_nt_file_t) + km_nt_file_padded_size(file->name));
         km_nt_file_t* fnote = (km_nt_file_t*)cur;
         cur += sizeof(km_nt_file_t);
         fnote->size = sizeof(km_nt_file_t);
         fnote->fd = file->guestfd;
         fnote->flags = file->flags;

         strcpy(cur, file->name);
         cur += km_nt_file_padded_size(file->name);

         struct stat st;
         if (fstat(file->hostfd, &st) < 0) {
            km_err_msg(errno, "fstat failed fd=%d(%d) name=%s", file->guestfd, file->hostfd, file->name);
            continue;
         }
         fnote->mode = st.st_mode;

         switch (st.st_mode & __S_IFMT) {
            case __S_IFREG:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFREG", i);
               fnote->position = lseek(file->hostfd, 0, SEEK_CUR);
               break;

            /*
             * TODO(muth): Fill in the rest of these cases.
             */
            case __S_IFDIR:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFDIR", i);
               break;

            case __S_IFLNK:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFLNK", i);
               break;

            case __S_IFCHR:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFCHR: %s", i, file->name);
               break;

            case __S_IFBLK:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFBLK: %s", i, file->name);
               break;

            case __S_IFIFO:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d IFIFO: %s", i, file->name);
               break;

            case __S_IFSOCK:
               km_infox(KM_TRACE_SNAPSHOT, "fd:%d ISOCK: %s", i, file->name);
               break;

            default:
               km_infox(KM_TRACE_SNAPSHOT,
                        "fd:%d mode %o NOT recognized: %s",
                        i,
                        st.st_mode & __S_IFMT,
                        file->name);
               break;
         }
      }
   }
   return cur - buf;
}

int km_fs_recover_open_file(char* ptr, size_t length)
{
   km_nt_file_t* nt_file = (km_nt_file_t*)ptr;
   if (nt_file->size != sizeof(km_nt_file_t)) {
      km_err_msg(0, "nt_km_file_t size mismatch - old snapshot?");
      return -1;
   }
   char* name = ptr + sizeof(km_nt_file_t);
   km_infox(KM_TRACE_SNAPSHOT, "fd=%d name=%s", nt_file->fd, name);

   /*
    * TODO: currently, the std fd's are always inherited from KM
    * for restored snapshots. This means we don't support
    * processes that mess with the std fd's. This needs to be
    * fixed.
    */
   // Std files always set by KM
   if (nt_file->fd <= 2) {
      return 0;
   }

   if (nt_file->fd < 0 || nt_file->fd >= km_fs()->nfdmap) {
      km_err_msg(EBADF, "bad file descriptor=%d", nt_file->fd);
      return -1;
   }

   int fd = open(name, nt_file->flags, 0);
   if (fd < 0) {
      km_err_msg(errno, "cannon open %s", name);
      return -1;
   }

   struct stat st;
   if (fstat(fd, &st) < 0) {
      km_err_msg(errno, "cannon fstat %s", name);
      return -1;
   }
   if (st.st_mode != nt_file->mode) {
      km_err_msg(0, "file mode mistmatch expect %o got %o %s", nt_file->mode, st.st_mode, name);
      return -1;
   }

   km_file_t* file = &km_fs()->guest_files[nt_file->fd];
   file->inuse = 1;
   file->guestfd = nt_file->fd;
   file->hostfd = fd;
   file->flags = nt_file->flags;
   file->name = strdup(name);

   km_fs()->hostfd_to_guestfd_map[fd] = nt_file->fd;

   if ((nt_file->mode & __S_IFMT) == __S_IFREG && nt_file->position != 0) {
      if (lseek(fd, nt_file->position, SEEK_SET) != nt_file->position) {
         km_err_msg(errno, "lseek failed");
         return -1;
      }
   }
   return 0;
}

// types for file names conversion
typedef struct {
   char* pattern;
   km_file_ops_t ops;
   regex_t regex;
} km_filename_table_t;

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

// called on the read of /proc/self/cmdline
static int proc_cmdline_read(int fd, char* buf, size_t buf_sz)
{
   char tmp[4096];
   if (read(fd, tmp, sizeof(tmp)) == 0) {
      return 0;   // second read, to make sure we are at end of file
   }
   // read till eof so on the second call we know we need to return 0 bytes
   while (read(fd, tmp, sizeof(tmp)) != 0) {
      ;
   }
   return km_exec_cmdline(buf, buf_sz);
}

static int proc_self_getdents(int fd, /* struct linux_dirent64* */ void* buf, size_t buf_sz)
{
   km_err_msg(0, "getdents on /proc/self or similar");
   return __syscall_3(SYS_getdents64, fd, (uint64_t)buf, buf_sz);
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
        .pattern = "^/proc/self$",
        .ops = {.getdents_g2h = proc_self_getdents},
    },
    {
        .pattern = "^/proc/self/sched$",
        .ops = {.read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/self/cmdline$",
        .ops = {.read_g2h = proc_cmdline_read},
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
        .pattern = "^/proc/%u$",
        .ops = {.getdents_g2h = proc_self_getdents},
    },
    {
        .pattern = "^/proc/%u/sched$",
        .ops = {.open_g2h = proc_sched_open, .read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/%u/cmdline$",
        .ops = {.read_g2h = proc_cmdline_read},
    },
    {},
};

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
      if (strncmp(t->pattern, PROC_PID, strlen(PROC_PID))) {
         char pat[128];
         snprintf(pat, sizeof(pat), t->pattern, machine.pid);
         if (regcomp(&t->regex, pat, REG_EXTENDED | REG_NOSUB) != 0) {
            err(1, "filename regcomp failed, exiting...");
         }
      } else {
         if (regcomp(&t->regex, t->pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            err(1, "filename regcomp failed, exiting...");
         }
      }
   }
}

static void km_fs_filename_fini(void)
{
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      regfree(&t->regex);
   }
}

int km_fs_init(void)
{
   struct rlimit lim;

   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -errno;
   }

   machine.filesys = calloc(1, sizeof(km_filesys_t));
   lim.rlim_cur = MAX_OPEN_FILES;   // Limit max open files. Temporary change till we support config.
   size_t mapsz = lim.rlim_cur * sizeof(int);

   km_infox(KM_TRACE_FILESYS, "lim.rlim_cur=%ld", lim.rlim_cur);
   km_fs()->nfdmap = lim.rlim_cur;
   km_fs()->guest_files = calloc(lim.rlim_cur, sizeof(km_file_t));
   assert(km_fs()->guest_files != NULL);

   km_fs()->hostfd_to_guestfd_map = malloc(mapsz);
   assert(km_fs()->hostfd_to_guestfd_map != NULL);
   memset(km_fs()->hostfd_to_guestfd_map, 0xff, mapsz);

   if (km_exec_recover_guestfd() != 0) {
      // setup guest std file streams.
      for (int i = 0; i < 3; i++) {
         km_file_t* file = &km_fs()->guest_files[i];
         file->inuse = 1;
         file->guestfd = i;
         file->hostfd = i;

         km_fs()->hostfd_to_guestfd_map[i] = i;
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
   if (km_fs()->hostfd_to_guestfd_map != NULL) {
      free(km_fs()->hostfd_to_guestfd_map);
      km_fs()->hostfd_to_guestfd_map = NULL;
   }
   free(machine.filesys);
}
