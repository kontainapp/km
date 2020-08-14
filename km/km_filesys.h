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
#include <sys/vfs.h>

#include "km.h"
#include "km_mem.h"
#include "km_syscall.h"

// types for file names conversion
typedef int (*km_file_open_t)(const char* guest_fn, char* host_fn, size_t host_fn_sz);
typedef int (*km_file_readlink_t)(const char* guest_fn, char* buf, size_t buf_sz);
typedef int (*km_file_read_t)(int fd, char* buf, size_t buf_sz);
typedef int (*km_file_getdents_t)(int fd, /* struct linux_dirent64 */ void* buf, size_t buf_sz);

// types for file names conversion
typedef struct {
   km_file_open_t open_g2h;
   km_file_read_t read_g2h;
   km_file_getdents_t getdents_g2h;
   km_file_readlink_t readlink_g2h;
} km_file_ops_t;

typedef struct {
   char* pattern;
   km_file_ops_t ops;
   regex_t regex;
} km_filename_table_t;

int km_filename_table_line(km_file_ops_t* o);
km_file_ops_t* km_file_ops(int i);

/*
 * maps a host fd to a guest fd. Returns a negative error number if mapping does
 * not exist. Used by SIGPIPE/SIGIO signal handlers and select.
 * Note: vcpu is NULL if called from km signal handler.
 */
int km_fs_h2g_fd(int hostfd);
int km_fs_g2h_fd(int guestfd, km_file_ops_t** ops);
int km_fs_max_guestfd();
int km_add_guest_fd(km_vcpu_t* vcpu, int host_fd, char* name, int flags, km_file_ops_t* ops);
char* km_guestfd_name(km_vcpu_t* vcpu, int fd);
int km_fs_init(void);
void km_fs_fini(void);
// int open(char *pathname, int flags, mode_t mode)
uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode);
// int openat(int dirfd, const char *pathname, int flags);
// int openat(int dirfd, const char* pathname, int flags, mode_t mode);
uint64_t km_fs_openat(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, mode_t mode);
// int close(fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd);
// int shutdown(int sockfd, int how);
int km_fs_shutdown(km_vcpu_t* vcpu, int sockfd, int how);
// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
uint64_t km_fs_prw(km_vcpu_t* vcpu, int scall, int fd, void* buf, size_t count, off_t offset);
// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
uint64_t
km_fs_prwv(km_vcpu_t* vcpu, int scall, int fd, const struct iovec* guest_iov, size_t iovcnt, off_t offset);
// int ioctl(int fd, unsigned long request, void *arg);
uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg);
// int fcntl(int fd, int cmd, ... /* arg */ );
uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg);
// off_t lseek(int fd, off_t offset, int whence);
uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence);
// int mknod(const char *pathname, mode_t mode, dev_t dev);
// int chown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_chown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid);
// int lchown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_lchown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid);
// int fchown(int fd, uid_t owner, gid_t group);
uint64_t km_fs_fchown(km_vcpu_t* vcpu, int fd, uid_t uid, gid_t gid);
uint64_t km_fs_mknod(km_vcpu_t* vcpu, char* pathname, mode_t mode, dev_t dev);
// int chmod(const char *pathname, mode_t mode);
uint64_t km_fs_chmod(km_vcpu_t* vcpu, char* pathname, mode_t mode);
// int fchmod(int fd, mode_t mode);
uint64_t km_fs_fchmod(km_vcpu_t* vcpu, int fd, mode_t mode);
// int unlink(const char *path);
uint64_t km_fs_unlink(km_vcpu_t* vcpu, char* pathname);
// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count);
// int link(const char *oldpath, const char *newpath);
uint64_t km_fs_link(km_vcpu_t* vcpu, char* old, char* new);
// int symlink(const char *target, const char *linkpath);
uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath);
// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz);
// ssize_t readlinkat(it dirfd, const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlinkat(km_vcpu_t* vcpu, int dirfd, char* pathname, char* buf, size_t bufsz);
// int getcwd(char *buf, size_t size);
uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t bufsz);
// int chdir(const char *path);
uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* pathname);
// int fchdir(const int fd);
uint64_t km_fs_fchdir(km_vcpu_t* vcpu, int fd);
// int truncate(const char *path, off_t length);
uint64_t km_fs_truncate(km_vcpu_t* vcpu, char* pathname, off_t length);
// int ftruncate(int fd, off_t length);
uint64_t km_fs_ftruncate(km_vcpu_t* vcpu, int fd, off_t length);
// int fsync(int fd);
uint64_t km_fs_fsync(km_vcpu_t* vcpu, int fd);
// int fdatasync(int fd);
uint64_t km_fs_fdatasync(km_vcpu_t* vcpu, int fd);
// int mkdir(const char *path, mode_t mode);
uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode);
// int rmdir(const char *path, mode_t mode);
uint64_t km_fs_rmdir(km_vcpu_t* vcpu, char* pathname);
// int rename(const char *oldpath, const char *newpath);
uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath);
// int stat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf);
// int lstat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf);
// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf);
// int fstat(int fd, struct stat *statbuf);
uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf);
// int statfs(int fd, struct statfs *statbuf);
uint64_t km_fs_statfs(km_vcpu_t* vcpu, char* pathname, struct statfs* statbuf);
// int fstatfs(int fd, struct statfs *statbuf);
uint64_t km_fs_fstatfs(km_vcpu_t* vcpu, int fd, struct statfs* statbuf);
// int access(const char *pathname, int mode);
uint64_t km_fs_access(km_vcpu_t* vcpu, const char* pathname, int mode);
// int dup(int oldfd);
uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd);
// int dup2(int oldfd, int newfd);
uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd);
// int dup3(int oldfd, int newfd, int flags);
uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags);
// int pipe(int pipefd[2]);
uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2]);
// int pipe2(int pipefd[2], int flags);
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags);
// int eventfd2(unsigned int initval, int flags);
uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags);
// int socket(int domain, int type, int protocol);
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol);
// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen);
// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen);
// ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
// ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
uint64_t km_fs_sendrecvmsg(km_vcpu_t* vcpu, int scall, int sockfd, struct msghdr* msg, int flag);
// ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
uint64_t km_fs_sendfile(km_vcpu_t* vcpu, int out_fd, int in_fd, off_t* offset, size_t count);
// ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, loff_t *off_out, size_t len,
// unsigned int flags);
uint64_t km_fs_copy_file_range(
    km_vcpu_t* vcpu, int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags);
// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t
km_fs_get_sock_peer_name(km_vcpu_t* vcpu, int hc, int sockfd, struct sockaddr* addr, socklen_t* addrlen);
// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen);
// int listen(int sockfd, int backlog)
uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog);
// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen);
// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_connect(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen);
// int socketpair(int domain, int type, int protocol, int sv[2]);
uint64_t km_fs_socketpair(km_vcpu_t* vcpu, int domain, int type, int protocol, int sv[2]);
// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
uint64_t
km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags);
// ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                const struct sockaddr *dest_addr, socklen_t addrlen);
uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                      int sockfd,
                      const void* buf,
                      size_t len,
                      int flags,
                      const struct sockaddr* addr,
                      socklen_t addrlen);
// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
uint64_t km_fs_recvfrom(km_vcpu_t* vcpu,
                        int sockfd,
                        void* buf,
                        size_t len,
                        int flags,
                        struct sockaddr* addr,
                        socklen_t* addrlen);
//  int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
uint64_t km_fs_select(km_vcpu_t* vcpu,
                      int nfds,
                      fd_set* readfds,
                      fd_set* writefds,
                      fd_set* exceptfds,
                      struct timeval* timeout);
// int poll(struct pollfd *fds, nfds_t nfds, int timeout);
uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout);
// int epoll_create1(int flags);
uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags);
// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event);
// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
//  const sigset_t *sigmask);
uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                           int epfd,
                           struct epoll_event* events,
                           int maxevents,
                           int timeout,
                           const sigset_t* sigmask,
                           int sigsetsize);
// int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);
uint64_t km_fs_prlimit64(km_vcpu_t* vcpu,
                         pid_t pid,
                         int resource,
                         const struct rlimit* new_limit,
                         struct rlimit* old_limit);

size_t km_fs_core_notes_length();
size_t km_fs_core_notes_write(char* cur, size_t remain);

void km_redirect_msgs(const char* name);
void km_close_stdio(int log_to_fd);

int km_internal_open(const char* name, int flag);
int km_internal_eventfd(unsigned int initval, int flags);
int km_internal_fd_ioctl(int fd, unsigned long reques, ...);
int km_gdb_listen(int domain, int type, int protocol);
int km_gdb_accept(int fd, struct sockaddr* addr, socklen_t* addrlen);
int km_mgt_listen(int domain, int type, int protocol);
int km_mgt_accept(int fd, struct sockaddr* addr, socklen_t* addrlen);

int km_fs_recover(char* ptr, size_t length);
#define KM_TRACE_FILESYS "filesys"
#endif
