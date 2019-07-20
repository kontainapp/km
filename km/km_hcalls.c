/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <linux/futex.h>

#include "km.h"
#include "km_filesys.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_syscall.h"

/*
 * User space (km) implementation of hypercalls.
 * These functions are called from kvm_vcpu_run() when guest makes hypercall
 * vmexit, currently via OUTL command.
 *
 * km_hcalls_init() registers hypercalls in the table indexed by hcall #
 * TODO: make registration configurable so only payload specific set of hcalls
 * is registered
 *
 * Normally Linux system calls take parameters and return results in registers.
 * RAX is system call number and return value. The remaining up to 6 args are
 * "D" "S" "d" "r10" "r8" "r9".
 *
 * Unfortunately we have no access to registers from the guest, so instead guest
 * puts the values that normally would go to registers into km_hc_args_t
 * structure. Syscall number is passed as an IO port number, result is returned
 * in hc_ret field.
 *
 * __syscall_X() simply picks the values from memory into registers and do the
 * system call on behalf of the guest.
 *
 * Some of the values passed in these arguments are guest addresses. There is no
 * pattern here, each system call has its own signature. We need to translate
 * the guest addresses to km view to make things work, using km_gva_to_kma() when
 * appropriate. There is no machinery, need to manually interpret each system
 * call. We paste signature in comment to make it a bit easier. Look into each
 * XXX_hcall() for examples.
 */

static inline uint64_t km_gva_to_kml(uint64_t gva)
{
   return (uint64_t)km_gva_to_kma(gva);
}

/*
 * guest code executed exit(status);
 */
static km_hc_ret_t exit_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   *status = arg->arg1;
   return HC_STOP;
}

/*
 * guest code executed exit_grp(status);
 */
static km_hc_ret_t exit_grp_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   *status = arg->arg1;
   return HC_ALLSTOP;
}

/*
 * read/write and pread/pwrite. The former ignores the 4th arg
 */
static km_hc_ret_t prw_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t read(int fd, void *buf, size_t count);
   // ssize_t write(int fd, const void *buf, size_t count);
   // ssize_t pread(int fd, void *buf, size_t count, off_t offset);
   // ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
   arg->hc_ret =
       km_fs_prw((km_vcpu_t*)vcpu, hc, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

/*
 * readv/writev and preadv/pwritev. The former ignores the 4th arg
 */
static km_hc_ret_t prwv_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret =
       km_fs_prwv((km_vcpu_t*)vcpu, hc, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t accept_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   arg->hc_ret =
       km_fs_accept((km_vcpu_t*)vcpu, arg->arg1, km_gva_to_kma(arg->arg2), km_gva_to_kma(arg->arg3));
   return HC_CONTINUE;
}

static km_hc_ret_t bind_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
   arg->hc_ret = km_fs_bind((km_vcpu_t*)vcpu, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t listen_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int listen(int sockfd, int backlog);
   arg->hc_ret = km_fs_listen((km_vcpu_t*)vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t socket_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int socket(int domain, int type, int protocol);
   arg->hc_ret = km_fs_socket((km_vcpu_t*)vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t getsockopt_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t
   // *optlen);
   arg->hc_ret = km_fs_getsockopt((km_vcpu_t*)vcpu,
                                  arg->arg1,
                                  arg->arg2,
                                  arg->arg3,
                                  km_gva_to_kma(arg->arg4),
                                  km_gva_to_kma(arg->arg5));
   return HC_CONTINUE;
}

static km_hc_ret_t setsockopt_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int setsockopt(int sockfd, int level, int optname, const void *optval,
   // socklen_t optlen);
   arg->hc_ret = km_fs_setsockopt((km_vcpu_t*)vcpu,
                                  arg->arg1,
                                  arg->arg2,
                                  arg->arg3,
                                  km_gva_to_kma(arg->arg4),
                                  arg->arg5);
   return HC_CONTINUE;
}

static km_hc_ret_t ioctl_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int ioctl(int fd, unsigned long request, void *arg);
   arg->hc_ret = km_fs_ioctl((km_vcpu_t*)vcpu, arg->arg1, arg->arg2, km_gva_to_kma(arg->arg3));
   return HC_CONTINUE;
}

static km_hc_ret_t stat_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int stat(const char *pathname, struct stat *statbuf);
   arg->hc_ret = km_fs_stat((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), km_gva_to_kma(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t statx_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf);
   arg->hc_ret = km_fs_statx((km_vcpu_t*)vcpu,
                             arg->arg1,
                             km_gva_to_kma(arg->arg2),
                             arg->arg3,
                             arg->arg4,
                             km_gva_to_kma(arg->arg5));
   return HC_CONTINUE;
}

static km_hc_ret_t fstat_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int fstat(int fd, struct stat *statbuf);
   arg->hc_ret = km_fs_fstat((km_vcpu_t*)vcpu, arg->arg1, km_gva_to_kma(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t getdirents_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
   arg->hc_ret = km_fs_getdents((km_vcpu_t*)vcpu, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t getcwd_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int getcwd(char *buf, size_t size);
   arg->hc_ret = km_fs_getcwd((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t close_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret = km_fs_close((km_vcpu_t*)vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t shutdown_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int shutdown(int sockfd, int how);
   arg->hc_ret = __syscall_2(hc, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t brk_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret = km_mem_brk(arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t futex_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   int op = arg->arg2;
   uint64_t timeout_or_val2;

   // int futex(int *uaddr, int futex_op, int val,
   //   const struct timespec *timeout,   /* or: uint32_t val2 */
   //   int *uaddr2, int val3);
   switch (op & 0xf) {
      case FUTEX_WAIT:
      case FUTEX_WAIT_BITSET:
      case FUTEX_LOCK_PI:
      case FUTEX_WAIT_REQUEUE_PI:
         timeout_or_val2 = km_gva_to_kml(arg->arg4);
         break;
      default:
         timeout_or_val2 = arg->arg4;
         break;
   }
   arg->hc_ret = __syscall_6(hc,
                             km_gva_to_kml(arg->arg1),
                             op,
                             arg->arg3,
                             timeout_or_val2,
                             km_gva_to_kml(arg->arg5),
                             arg->arg6);
   return HC_CONTINUE;
}

static km_hc_ret_t mmap_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
   arg->hc_ret = km_guest_mmap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return HC_CONTINUE;
};

static km_hc_ret_t munmap_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int munmap(void *addr, size_t length);
   arg->hc_ret = km_guest_munmap(arg->arg1, arg->arg2);
   return HC_CONTINUE;
};

static km_hc_ret_t mremap_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /* void
   // *new_address */);
   arg->hc_ret = km_guest_mremap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return HC_CONTINUE;
};

static km_hc_ret_t mprotect_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   //  int mprotect(void *addr, size_t len, int prot);
   arg->hc_ret = km_guest_mprotect(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
};

static km_hc_ret_t clock_gettime_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int clock_gettime(clockid_t clk_id, struct timespec *tp);
   arg->hc_ret = __syscall_2(hc, arg->arg1, km_gva_to_kml(arg->arg2));
   return HC_CONTINUE;
}

/*
 * TODO: only see advice 4, MADV_DONTNEED so far. Might want to pass that to the host to expedite
 * freeing resources, assuming it has to be our memory
 */
static km_hc_ret_t madvise_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int madvise(void *addr, size_t length, int advice);
   km_infox(KM_TRACE_HC, "hc = %d (madvise), %ld %lx %lx", hc, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = 0;
   return HC_CONTINUE;
}

static km_hc_ret_t readlink_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
   arg->hc_ret =
       km_fs_readlink((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), km_gva_to_kma(arg->arg2), arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t getrandom_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
   arg->hc_ret = __syscall_3(hc, km_gva_to_kml(arg->arg1), arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t open_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int open(const char *pathname, int flags, mode_t mode);
   arg->hc_ret = km_fs_open((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t lseek_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // off_t lseek(int fd, off_t offset, int whence);
   arg->hc_ret = km_fs_lseek((km_vcpu_t*)vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t rename_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int rename(const char *oldpath, const char *newpath);
   arg->hc_ret = km_fs_rename((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), km_gva_to_kma(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t chdir_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int chdir(const char *path);
   arg->hc_ret = km_fs_chdir((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1));
   return HC_CONTINUE;
}

static km_hc_ret_t mkdir_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int mkdir(const char *path, mode_t mode);
   arg->hc_ret = km_fs_mkdir((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t select_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   //  int select(int nfds, fd_set *readfds, fd_set *writefds,
   //             fd_set *exceptfds, struct timeval *timeout);
   arg->hc_ret = km_fs_select((km_vcpu_t*)vcpu,
                              arg->arg1,
                              km_gva_to_kma(arg->arg2),
                              km_gva_to_kma(arg->arg3),
                              km_gva_to_kma(arg->arg4),
                              km_gva_to_kma(arg->arg5));
   return HC_CONTINUE;
}

static km_hc_ret_t sendto_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
   //                const struct sockaddr *dest_addr, socklen_t addrlen);
   arg->hc_ret = km_fs_sendto((km_vcpu_t*)vcpu,
                              arg->arg1,
                              km_gva_to_kma(arg->arg2),
                              arg->arg3,
                              arg->arg4,
                              km_gva_to_kma(arg->arg5),
                              arg->arg6);
   return HC_CONTINUE;
}

static km_hc_ret_t nanosleep_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int nanosleep(const struct timespec* req, struct timespec* rem);
   arg->hc_ret = __syscall_2(hc, km_gva_to_kml(arg->arg1), km_gva_to_kml(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t dup_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int dup(int oldfd);
   arg->hc_ret = km_fs_dup((km_vcpu_t*)vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t dup3_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int dup3(int oldfd, int newfd, int flags);
   arg->hc_ret = km_fs_dup3((km_vcpu_t*)vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t pause_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int pause(void);
   arg->hc_ret = __syscall_0(hc);
   return HC_CONTINUE;
}

static km_hc_ret_t getsockname_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   arg->hc_ret =
       km_fs_getsockname((km_vcpu_t*)vcpu, arg->arg1, km_gva_to_kma(arg->arg2), km_gva_to_kma(arg->arg3));
   return HC_CONTINUE;
}

static km_hc_ret_t poll_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int poll(struct pollfd *fds, nfds_t nfds, int timeout);
   arg->hc_ret = km_fs_poll((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t accept4_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int accept4(int sockfd, struct sockaddr *addr,
   //             socklen_t *addrlen, int flags);
   arg->hc_ret = km_fs_accept4((km_vcpu_t*)vcpu,
                               arg->arg1,
                               km_gva_to_kma(arg->arg2),
                               km_gva_to_kma(arg->arg3),
                               arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t recvfrom_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
   //                  struct sockaddr *src_addr, socklen_t *addrlen);
   arg->hc_ret = km_fs_recvfrom((km_vcpu_t*)vcpu,
                                arg->arg1,
                                km_gva_to_kma(arg->arg2),
                                arg->arg3,
                                arg->arg4,
                                km_gva_to_kma(arg->arg5),
                                km_gva_to_kma(arg->arg6));
   return HC_CONTINUE;
}

static km_hc_ret_t dummy_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret = 0;
   km_infox(KM_TRACE_HC,
            "hc = %d (dummy), %ld %lx %lx %lx %lx",
            hc,
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4,
            arg->arg5);
   return HC_CONTINUE;
}

static km_hc_ret_t pthread_create_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
   //   void *(*start_routine) (void *), void *arg);
   km_kma_t pt = 0;
   km_kma_t attr = 0;

   if (arg->arg1 != 0 && (pt = km_gva_to_kma(arg->arg1)) == 0) {
      arg->hc_ret = EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg2 != 0 && (attr = km_gva_to_kma(arg->arg2)) == 0) {
      arg->hc_ret = EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_pthread_create(vcpu, pt, attr, arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t pthread_join_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int pthread_join(pthread_t thread, void **retval);
   km_kma_t pt = 0;
   if (arg->arg2 != 0 && (pt = km_gva_to_kma(arg->arg2)) == 0) {
      arg->hc_ret = EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_pthread_join(vcpu, arg->arg1, pt);
   return HC_CONTINUE;
}

static km_hc_ret_t guest_interrupt_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   km_handle_interrupt((km_vcpu_t*)vcpu);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigprocmask_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int rt_sigprocmask(int how, const kernel_sigset_t *set, kernel_sigset_t *oldset, size_t sigsetsize);
   km_sigset_t* set = NULL;
   km_sigset_t* oldset = NULL;

   if (arg->arg2 != 0 && (set = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   if (arg->arg3 != 0 && (oldset = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigprocmask(vcpu, arg->arg1, set, oldset, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigaction_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
   km_sigaction_t* act = NULL;
   km_sigaction_t* oldact = NULL;

   if (arg->arg2 != 0 && (act = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   if (arg->arg3 != 0 && (oldact = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = EINVAL;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigaction(vcpu, arg->arg1, act, oldact, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigreturn_hcall(void* varg, int hc, km_hc_args_t* arg, int* status)
{
   km_vcpu_t* vcpu = (km_vcpu_t*)varg;

   km_rt_sigreturn(vcpu);   // don't care about arg or return code.
   return HC_CONTINUE;
}

static km_hc_ret_t kill_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int kill(pid_t pid, int sig)
   arg->hc_ret = km_kill(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t tkill_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int tkill(pid_t tid, int sig)
   arg->hc_ret = km_tkill(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigpending_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int rt_sigpending(sigset_t *set, size_t sigsetsize)
   arg->hc_ret = km_rt_sigpending(vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll1_create_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int epoll_create1(int flags);
   arg->hc_ret = km_fs_epoll_create1((km_vcpu_t*)vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll_ctl_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
   arg->hc_ret =
       km_fs_epoll_ctl((km_vcpu_t*)vcpu, arg->arg1, arg->arg2, arg->arg3, km_gva_to_kma(arg->arg4));
   return HC_CONTINUE;
}

static km_hc_ret_t epoll_pwait_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const
   // sigset_t *sigmask);
   arg->hc_ret = km_fs_epoll_pwait((km_vcpu_t*)vcpu,
                                   arg->arg1,
                                   km_gva_to_kma(arg->arg2),
                                   arg->arg3,
                                   arg->arg4,
                                   km_gva_to_kma(arg->arg5));
   return HC_CONTINUE;
}

static km_hc_ret_t pipe_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int pipe(int pipefd[2]);
   arg->hc_ret = km_fs_pipe((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1));
   return HC_CONTINUE;
}

static km_hc_ret_t pipe2_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int pipe2(int pipefd[2], int flags);
   arg->hc_ret = km_fs_pipe2((km_vcpu_t*)vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t eventfd2_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int eventfd(unsigned int initval, int flags);
   arg->hc_ret = km_fs_eventfd((km_vcpu_t*)vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t prlimit64_hcall(void* vcpu, int hc, km_hc_args_t* arg, int* status)
{
   // int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit
   // *old_limit);
   arg->hc_ret =
       __syscall_4(hc, arg->arg1, arg->arg2, km_gva_to_kml(arg->arg3), km_gva_to_kml(arg->arg4));
   return HC_CONTINUE;
}

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
km_hcall_fn_t km_hcalls_table[KM_MAX_HCALL];

void km_hcalls_init(void)
{
   km_hcalls_table[SYS_exit] = exit_hcall;
   km_hcalls_table[SYS_exit_group] = exit_grp_hcall;
   km_hcalls_table[SYS_read] = prw_hcall;
   km_hcalls_table[SYS_write] = prw_hcall;
   km_hcalls_table[SYS_readv] = prwv_hcall;
   km_hcalls_table[SYS_writev] = prwv_hcall;
   km_hcalls_table[SYS_pread64] = prw_hcall;
   km_hcalls_table[SYS_pwrite64] = prw_hcall;
   km_hcalls_table[SYS_preadv] = prwv_hcall;
   km_hcalls_table[SYS_pwritev] = prwv_hcall;
   km_hcalls_table[SYS_accept] = accept_hcall;
   km_hcalls_table[SYS_bind] = bind_hcall;
   km_hcalls_table[SYS_listen] = listen_hcall;
   km_hcalls_table[SYS_socket] = socket_hcall;
   km_hcalls_table[SYS_getsockopt] = getsockopt_hcall;
   km_hcalls_table[SYS_setsockopt] = setsockopt_hcall;
   km_hcalls_table[SYS_ioctl] = ioctl_hcall;
   km_hcalls_table[SYS_fcntl] = ioctl_hcall;
   km_hcalls_table[SYS_stat] = stat_hcall;
   km_hcalls_table[SYS_lstat] = stat_hcall;
   km_hcalls_table[SYS_statx] = statx_hcall;
   km_hcalls_table[SYS_fstat] = fstat_hcall;
   km_hcalls_table[SYS_getdents64] = getdirents_hcall;
   km_hcalls_table[SYS_getcwd] = getcwd_hcall;
   km_hcalls_table[SYS_close] = close_hcall;
   km_hcalls_table[SYS_shutdown] = shutdown_hcall;
   km_hcalls_table[SYS_brk] = brk_hcall;
   km_hcalls_table[SYS_futex] = futex_hcall;
   km_hcalls_table[SYS_mmap] = mmap_hcall;
   km_hcalls_table[SYS_munmap] = munmap_hcall;
   km_hcalls_table[SYS_mremap] = mremap_hcall;
   km_hcalls_table[SYS_mprotect] = mprotect_hcall;
   km_hcalls_table[SYS_clock_gettime] = clock_gettime_hcall;
   km_hcalls_table[SYS_madvise] = madvise_hcall;

   km_hcalls_table[SYS_readlink] = readlink_hcall;
   km_hcalls_table[SYS_getrandom] = getrandom_hcall;
   km_hcalls_table[SYS_open] = open_hcall;
   km_hcalls_table[SYS_lseek] = lseek_hcall;
   km_hcalls_table[SYS_rename] = rename_hcall;
   km_hcalls_table[SYS_mkdir] = mkdir_hcall;
   km_hcalls_table[SYS_chdir] = chdir_hcall;
   km_hcalls_table[SYS_select] = select_hcall;
   km_hcalls_table[SYS_pause] = pause_hcall;
   km_hcalls_table[SYS_sendto] = sendto_hcall;
   km_hcalls_table[SYS_nanosleep] = nanosleep_hcall;
   km_hcalls_table[SYS_getsockname] = getsockname_hcall;
   km_hcalls_table[SYS_poll] = poll_hcall;
   km_hcalls_table[SYS_accept4] = accept4_hcall;
   km_hcalls_table[SYS_recvfrom] = recvfrom_hcall;
   km_hcalls_table[SYS_epoll_create1] = epoll1_create_hcall;
   km_hcalls_table[SYS_epoll_ctl] = epoll_ctl_hcall;
   km_hcalls_table[SYS_epoll_pwait] = epoll_pwait_hcall;
   km_hcalls_table[SYS_dup] = dup_hcall;
   km_hcalls_table[SYS_dup3] = dup3_hcall;
   km_hcalls_table[SYS_pipe] = pipe_hcall;
   km_hcalls_table[SYS_pipe2] = pipe2_hcall;
   km_hcalls_table[SYS_eventfd2] = eventfd2_hcall;
   km_hcalls_table[SYS_prlimit64] = prlimit64_hcall;

   km_hcalls_table[SYS_rt_sigprocmask] = rt_sigprocmask_hcall;
   km_hcalls_table[SYS_rt_sigaction] = rt_sigaction_hcall;
   km_hcalls_table[SYS_rt_sigreturn] = rt_sigreturn_hcall;
   km_hcalls_table[SYS_rt_sigpending] = rt_sigpending_hcall;
   km_hcalls_table[SYS_kill] = kill_hcall;
   km_hcalls_table[SYS_tkill] = tkill_hcall;

   km_hcalls_table[SYS_getpid] = dummy_hcall;
   km_hcalls_table[SYS_getppid] = dummy_hcall;
   km_hcalls_table[SYS_geteuid] = dummy_hcall;
   km_hcalls_table[SYS_getuid] = dummy_hcall;
   km_hcalls_table[SYS_getegid] = dummy_hcall;
   km_hcalls_table[SYS_getgid] = dummy_hcall;
   km_hcalls_table[SYS_sched_yield] = dummy_hcall;

   km_hcalls_table[SYS_clock_getres] = dummy_hcall;

   km_hcalls_table[HC_pthread_create] = pthread_create_hcall;
   km_hcalls_table[HC_pthread_join] = pthread_join_hcall;
   km_hcalls_table[HC_guest_interrupt] = guest_interrupt_hcall;
}

void km_hcalls_fini(void)
{
   /* empty for now */
}
