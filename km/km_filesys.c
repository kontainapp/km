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
#include "km_filesys.h"
#include "km_mem.h"
#include "km_syscall.h"

#define MAX_OPEN_FILES (1024)
/*
 * Adds a host fd to the guest. Returns the guest fd number assigned.
 * Assigns lowest available guest fd, just like the kernel.
 */
static int add_guest_fd(km_vcpu_t* vcpu, int host_fd, int start_guestfd, char* name)
{
   assert(host_fd >= 0 && host_fd < machine.filesys.nfdmap);
   assert(start_guestfd >= 0 && start_guestfd < machine.filesys.nfdmap);
   int guest_fd = -1;
   for (int i = start_guestfd; i < machine.filesys.nfdmap; i++) {
      int fd_available = -1;
      if (__atomic_compare_exchange_n(&machine.filesys.guestfd_to_hostfd_map[i],
                                      &fd_available,
                                      host_fd,
                                      0,
                                      __ATOMIC_SEQ_CST,
                                      __ATOMIC_SEQ_CST) != 0) {
         __atomic_store_n(&machine.filesys.hostfd_to_guestfd_map[host_fd], i, __ATOMIC_SEQ_CST);
         void* newval = NULL;
         if (name != NULL) {
            newval = strdup(name);
            assert(newval != NULL);
         }
         void* oldval = NULL;
         __atomic_exchange(&machine.filesys.guestfd_to_name_map[i], &newval, &oldval, __ATOMIC_SEQ_CST);
         if (oldval != NULL) {
            free(oldval);
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
   assert(hostfd >= 0 && hostfd < machine.filesys.nfdmap);
   int rc = __atomic_compare_exchange_n(&machine.filesys.hostfd_to_guestfd_map[hostfd],
                                        &guestfd,
                                        -1,
                                        0,
                                        __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST);
   assert(rc != 0);

   assert(guestfd >= 0 && guestfd < machine.filesys.nfdmap);
   rc = __atomic_compare_exchange_n(&machine.filesys.guestfd_to_hostfd_map[guestfd],
                                    &hostfd,
                                    -1,
                                    0,
                                    __ATOMIC_SEQ_CST,
                                    __ATOMIC_SEQ_CST);
   assert(rc != 0);

   void* newval = NULL;
   void* oldval = NULL;
   __atomic_exchange(&machine.filesys.guestfd_to_name_map[guestfd], &newval, &oldval, __ATOMIC_SEQ_CST);
   if (oldval != NULL) {
      free(oldval);
   }
}

char* km_guestfd_name(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= machine.filesys.nfdmap) {
      return NULL;
   }
   return machine.filesys.guestfd_to_name_map[fd];
}

/*
 * Replaces the mapping for a guest file descriptor. Used by dup2(2) and dup(3).
 */
static int replace_guest_fd(km_vcpu_t* vcpu, int guest_fd, int host_fd)
{
   assert(guest_fd >= 0 && guest_fd < machine.filesys.nfdmap);
   assert(host_fd >= 0 && host_fd < machine.filesys.nfdmap);
   int close_fd =
       __atomic_exchange_n(&machine.filesys.guestfd_to_hostfd_map[guest_fd], host_fd, __ATOMIC_SEQ_CST);
   __atomic_store_n(&machine.filesys.hostfd_to_guestfd_map[host_fd], guest_fd, __ATOMIC_SEQ_CST);
   // don't close stdin, stdout, or stderr
   if (close_fd > 2) {
      __syscall_1(SYS_close, close_fd);
   }
   return guest_fd;
}

/*
 * maps a host fd to a guest fd. Returns a negative error number if mapping does
 * not exist. Used by SIGPIPE/SIGIO signal handlers and select.
 * Note: vcpu is NULL if called from km signal handler.
 */
int hostfd_to_guestfd(km_vcpu_t* vcpu, int hostfd)
{
   if (hostfd < 0) {
      return -ENOENT;
   }
   int guest_fd = __atomic_load_n(&machine.filesys.hostfd_to_guestfd_map[hostfd], __ATOMIC_SEQ_CST);
   if (__atomic_load_n(&machine.filesys.guestfd_to_hostfd_map[guest_fd], __ATOMIC_SEQ_CST) != hostfd) {
      guest_fd = -ENOENT;
   }
   return guest_fd;
}

/*
 * Translates guest fd to host fd. Returns negative errno if
 * mapping does not exist.
 */
int guestfd_to_hostfd(int fd)
{
   if (fd < 0 || fd >= machine.filesys.nfdmap) {
      return -1;
   }
   int ret = __atomic_load_n(&machine.filesys.guestfd_to_hostfd_map[fd], __ATOMIC_SEQ_CST);
   assert((ret == -1) || (machine.filesys.hostfd_to_guestfd_map[ret] == fd) ||
          (machine.filesys.hostfd_to_guestfd_map[ret] == -1));
   return ret;
}

int km_fs_init(void)
{
   struct rlimit lim;

   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -errno;
   }

   lim.rlim_cur = MAX_OPEN_FILES;   // Limit max open files. Temporary change till we support config.
   size_t mapsz = lim.rlim_cur * sizeof(int);

   machine.filesys.guestfd_to_hostfd_map = malloc(mapsz);
   assert(machine.filesys.guestfd_to_hostfd_map != NULL);
   memset(machine.filesys.guestfd_to_hostfd_map, 0xff, mapsz);
   machine.filesys.hostfd_to_guestfd_map = malloc(mapsz);
   assert(machine.filesys.hostfd_to_guestfd_map != NULL);
   memset(machine.filesys.hostfd_to_guestfd_map, 0xff, mapsz);
   machine.filesys.nfdmap = lim.rlim_cur;
   machine.filesys.guestfd_to_name_map = calloc(lim.rlim_cur, sizeof(char*));
   assert(machine.filesys.guestfd_to_name_map != NULL);

   // setup guest std file streams.
   for (int i = 0; i < 3; i++) {
      machine.filesys.guestfd_to_hostfd_map[i] = i;
      machine.filesys.hostfd_to_guestfd_map[i] = i;
      switch (i) {
         case 0:
            machine.filesys.guestfd_to_name_map[i] = strdup("[stdin]");
            break;
         case 1:
            machine.filesys.guestfd_to_name_map[i] = strdup("[stdout]");
            break;
         case 2:
            machine.filesys.guestfd_to_name_map[i] = strdup("[stderr]");
            break;
      }
      assert(machine.filesys.guestfd_to_name_map[i] != NULL);
   }
   return 0;
}

void km_fs_fini(void)
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
uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode)
{
   int guestfd = -1;
   int hostfd = __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
   if (hostfd >= 0) {
      guestfd = add_guest_fd(vcpu, hostfd, 0, pathname);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "open(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

// int close(fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
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
   // stdin, stdout, and stderr shared with KM so guest can't close them.
   if (fd > 2) {
      ret = __syscall_1(SYS_close, host_fd);
   } else {
      warnx("guest closing fd=%d", fd);
   }
   if (ret != 0) {
      warnx("close of guest fd %d (hostfd %d) returned an error: %d", fd, host_fd, ret);
   }
   return ret;
}

// int shutdown(int sockfd, int how);
int km_fs_shutdown(km_vcpu_t* vcpu, int sockfd, int how)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_shutdown, host_fd, how);
   return ret;
}

// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
uint64_t km_fs_prw(km_vcpu_t* vcpu, int scall, int fd, const void* buf, size_t count, off_t offset)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(scall, host_fd, (uintptr_t)buf, count, offset);
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
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
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
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_ioctl, host_fd, request, (uintptr_t)arg);
   return ret;
}

// int fcntl(int fd, int cmd, ... /* arg */ );
uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
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
   if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      if (ret >= 0) {
         ret = add_guest_fd(vcpu, ret, arg, km_guestfd_name(vcpu, fd));
      }
   }
   return ret;
}

// off_t lseek(int fd, off_t offset, int whence);
uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_lseek, host_fd, offset, whence);
   return ret;
}

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_getdents64, host_fd, (uintptr_t)dirp, count);
   return ret;
}

// int symlink(const char *target, const char *linkpath);
uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath)
{
   int ret = __syscall_2(SYS_symlink, (uintptr_t)target, (uintptr_t)linkpath);
   return ret;
}

// int link(const char *oldpath, const char *newpath);
uint64_t km_fs_link(km_vcpu_t* vcpu, char* old, char* new)
{
   int ret = __syscall_2(SYS_link, (uintptr_t)old, (uintptr_t) new);
   return ret;
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz)
{
   int ret = __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
   km_infox(KM_TRACE_FILESYS, "%s buf: %s", pathname, buf);
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
   int ret = __syscall_1(SYS_chdir, (uintptr_t)pathname);
   return ret;
}

// int fchdir(int fd);
uint64_t km_fs_fchdir(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_fchdir, host_fd);
   return ret;
}

// int truncate(const char *path, off_t length);
uint64_t km_fs_truncate(km_vcpu_t* vcpu, char* pathname, off_t length)
{
   int ret = __syscall_2(SYS_truncate, (uintptr_t)pathname, length);
   return ret;
}

// int ftruncate(int fd, off_t length);
uint64_t km_fs_ftruncate(km_vcpu_t* vcpu, int fd, off_t length)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_ftruncate, host_fd, length);
   return ret;
}

// int fsync(int fd);
uint64_t km_fs_fsync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_fsync, host_fd);
   return ret;
}

// int fdatasync(int fd);
uint64_t km_fs_fdatasync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_1(SYS_fdatasync, host_fd);
   return ret;
}

// int mkdir(const char *path, mode_t mode);
uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   int ret = __syscall_2(SYS_mkdir, (uintptr_t)pathname, mode);
   return ret;
}

// int rmdir(const char *path);
uint64_t km_fs_rmdir(km_vcpu_t* vcpu, char* pathname)
{
   int ret = __syscall_1(SYS_rmdir, (uintptr_t)pathname);
   return ret;
}

// int unlink(const char *path);
uint64_t km_fs_unlink(km_vcpu_t* vcpu, char* pathname)
{
   int ret = __syscall_1(SYS_unlink, (uintptr_t)pathname);
   return ret;
}

// int mknod(const char *pathname, mode_t mode, dev_t dev);
uint64_t km_fs_mknod(km_vcpu_t* vcpu, char* pathname, mode_t mode, dev_t dev)
{
   int ret = __syscall_3(SYS_mknod, (uintptr_t)pathname, mode, dev);
   return ret;
}

// int chown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_chown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   int ret = __syscall_3(SYS_chown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int lchown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_lchown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   int ret = __syscall_3(SYS_lchown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int fchown(int fd, uid_t owner, gid_t group);
uint64_t km_fs_fchown(km_vcpu_t* vcpu, int fd, uid_t uid, gid_t gid)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_fchown, host_fd, uid, gid);
   return ret;
}

// int chmod(const char *pathname, mode_t mode);
uint64_t km_fs_chmod(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   int ret = __syscall_2(SYS_chmod, (uintptr_t)pathname, mode);
   return ret;
}

// int fchmod(int fd, mode_t mode);
uint64_t km_fs_fchmod(km_vcpu_t* vcpu, int fd, mode_t mode)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_fchmod, host_fd, mode);
   return ret;
}

// int rename(const char *oldpath, const char *newpath);
uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   int ret = __syscall_2(SYS_rename, (uintptr_t)oldpath, (uintptr_t)newpath);
   return ret;
}

// int stat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   int ret = __syscall_2(SYS_stat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int lstat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   int ret = __syscall_2(SYS_lstat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   int host_fd = dirfd;

   if (dirfd != AT_FDCWD && pathname[0] != '/') {
      if ((host_fd = guestfd_to_hostfd(dirfd)) < 0) {
         return -EBADF;
      }
   }
   int ret = __syscall_5(SYS_statx, host_fd, (uintptr_t)pathname, flags, mask, (uintptr_t)statxbuf);
   return ret;
}

// int fstat(int fd, struct stat *statbuf);
uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
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
   int ret = __syscall_2(SYS_access, (uintptr_t)pathname, mode);
   return ret;
}

// int dup(int oldfd);
uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   char* name = km_guestfd_name(vcpu, fd);
   assert(name != NULL);
   int ret = __syscall_1(SYS_dup, host_fd);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret, 0, name);
   }
   return ret;
}

// int dup2(int oldfd, int newfd);
uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   if (newfd < 0 || newfd >= machine.filesys.nfdmap) {
      return -EBADF;
   }
   int host_newfd;
   if ((host_newfd = guestfd_to_hostfd(newfd)) < 0) {
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
uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags)
{
   int host_fd;
   if ((host_fd = guestfd_to_hostfd(fd)) < 0) {
      return -EBADF;
   }
   int host_newfd;
   if ((host_newfd = guestfd_to_hostfd(newfd)) < 0) {
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
uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   int ret = __syscall_1(SYS_pipe, (uintptr_t)pipefd);
   if (ret == 0) {
      pipefd[0] = add_guest_fd(vcpu, pipefd[0], 0, "[pipe[0]]");
      pipefd[1] = add_guest_fd(vcpu, pipefd[1], 0, "[pipe[1]]");
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int ret = __syscall_2(SYS_pipe2, (uintptr_t)pipefd, flags);
   if (ret == 0) {
      pipefd[0] = add_guest_fd(vcpu, pipefd[0], 1, "[pipe2[0]]");
      pipefd[1] = add_guest_fd(vcpu, pipefd[1], 0, "[pipe2[0]]");
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret, 0, "[eventfd2]");
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret, 0, "[socket]");
   }
   return ret;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
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
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
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
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(scall, host_sockfd, (uintptr_t)msg, flag);
   return ret;
}

// ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
uint64_t km_fs_sendfile(km_vcpu_t* vcpu, int out_fd, int in_fd, off_t* offset, size_t count)
{
   int host_outfd, host_infd;
   if ((host_outfd = guestfd_to_hostfd(out_fd)) < 0) {
      return -EBADF;
   }
   if ((host_infd = guestfd_to_hostfd(in_fd)) < 0) {
      return -EBADF;
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
   if ((host_outfd = guestfd_to_hostfd(fd_out)) < 0) {
      return -EBADF;
   }
   if ((host_infd = guestfd_to_hostfd(fd_in)) < 0) {
      return -EBADF;
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
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(hc, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   return ret;
}

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_bind, host_sockfd, (uintptr_t)addr, addrlen);
   return ret;
}

// int listen(int sockfd, int backlog)
uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_listen, host_sockfd, backlog);
   return ret;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_accept, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret, 0, "[accept]");
   }
   return ret;
}

// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_connect(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
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
      sv[0] = add_guest_fd(vcpu, sv[0], 0, "[socketpair[0]]");
      sv[1] = add_guest_fd(vcpu, sv[1], 0, "[socketpair[1]]");
   }
   return ret;
}

// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
   int host_sockfd;
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_accept4, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen, flags);
   if (ret >= 0) {
      ret = add_guest_fd(vcpu, ret, 0, "[accept4]");
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
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
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
   if ((host_sockfd = guestfd_to_hostfd(sockfd)) < 0) {
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
uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   struct pollfd host_fds[nfds];

   // fds checked before this is called.
   for (int i = 0; i < nfds; i++) {
      if ((host_fds[i].fd = guestfd_to_hostfd(fds[i].fd)) < 0) {
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
      ret = add_guest_fd(vcpu, ret, 0, "[epoll_create1]");
   }
   return ret;
}

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   int host_epfd;
   int host_fd;

   if ((host_epfd = guestfd_to_hostfd(epfd)) < 0 || (host_fd = guestfd_to_hostfd(fd)) < 0) {
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
   if ((host_epfd = guestfd_to_hostfd(epfd)) < 0) {
      return -EBADF;
   }

   int ret = __syscall_6(SYS_epoll_wait,
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
   if (resource == RLIMIT_NOFILE && new_limit != NULL && new_limit->rlim_cur > machine.filesys.nfdmap) {
      return -EPERM;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   return ret;
}

// procfdname() produces /proc/self/fd/<number> name for given fd
uint64_t km_fs_procfdname(km_vcpu_t* vcpu, char* buf, int fd)
{
   int host_fd = guestfd_to_hostfd(fd);
   if (host_fd < 0) {
      strcpy(buf, "/proc/nonexistent");
      return -EBADF;
   }
   /*
    * This is used from inside musl, for instance in an implementation of realpath(). We obviously
    * need to transate the fd number so we make it into a pypercall. We could've make realpath()
    * into a hypercall but procfdname() is used in half a dozen other places in musl.
    *
    * The buffer is provided with (alomst) fixed size expressed as ``char buf[15+3*sizeof(int)];''
    * or once as `char procname[sizeof "/proc/self/fd/" + 3*sizeof(int) + 2];'' so we play along.
    */
   sprintf(buf, "/proc/self/fd/%d", host_fd);
   return 0;
}