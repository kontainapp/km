/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <asm/prctl.h>
#include <linux/futex.h>
#include <linux/stat.h>

#include "km.h"
#include "km_coredump.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_fork.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"
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
 * FS reg used to store guest_thread pointer
 */
static km_hc_ret_t arch_prctl_hcall(void* v, int hc, km_hc_args_t* arg)
{
   // int arch_prctl(int code, unsigned long addr);
   // int arch_prctl(int code, unsigned long* addr);
   km_vcpu_t* vcpu = v;
   int code = arg->arg1;

   switch (code) {
      case ARCH_SET_FS:
         if (km_gva_to_kma(arg->arg2) == NULL) {   // just to check, FS gets gva value
            arg->hc_ret = -EPERM;
         } else {
            vcpu->guest_thr = arg->arg2;
            km_read_sregisters(vcpu);
            vcpu->sregs.fs.base = vcpu->guest_thr;
            km_write_sregisters(vcpu);
            arg->hc_ret = 0;
         }
         break;

      case ARCH_GET_FS: {
         km_kma_t addr = km_gva_to_kma(arg->arg2);
         if (addr == NULL) {
            arg->hc_ret = -EFAULT;
         } else {
            *(u_int64_t*)addr = vcpu->guest_thr;
            arg->hc_ret = 0;
         }
         break;
      }
      default:
         arg->hc_ret = -ENOTSUP;
         break;
   }
   return HC_CONTINUE;
}

/*
 * guest code executed exit(status);
 */
static km_hc_ret_t exit_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   return HC_STOP;
}

/*
 * guest code executed exit_grp(status);
 */
static km_hc_ret_t exit_grp_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   machine.exit_status = arg->arg1;
   return HC_ALLSTOP;
}

/*
 * read/write and pread/pwrite. The former ignores the 4th arg
 */
static km_hc_ret_t prw_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t read(int fd, void *buf, size_t count);
   // ssize_t write(int fd, const void *buf, size_t count);
   // ssize_t pread(int fd, void *buf, size_t count, off_t offset);
   // ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
   // arg->hc_ret = __syscall_4(hc, arg->arg1, km_gva_to_kml(arg->arg2), arg->arg3, arg->arg4);
   void* buf = km_gva_to_kma(arg->arg2);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // arg->hc_ret = km_fs_prw(vcpu, hc, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3, arg->arg4);
   arg->hc_ret = km_fs_prw(vcpu, hc, arg->arg1, buf, arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

/*
 * readv/writev and preadv/pwritev. The former ignores the 4th arg
 */
static km_hc_ret_t prwv_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   void* buf = km_gva_to_kma(arg->arg2);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_prwv(vcpu, hc, arg->arg1, buf, arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t accept_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   void* addr = km_gva_to_kma(arg->arg2);
   void* addrlen = km_gva_to_kma(arg->arg3);
   if (addr == NULL || addrlen == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_accept(vcpu, arg->arg1, addr, addrlen);
   return HC_CONTINUE;
}

static km_hc_ret_t socketpair_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int socketpair(int domain, int type, int protocol, int sv[2]);
   void* sv = km_gva_to_kma(arg->arg4);
   if (sv == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_socketpair(vcpu, arg->arg1, arg->arg2, arg->arg3, sv);
   return HC_CONTINUE;
}

static km_hc_ret_t connect_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   //  int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
   void* addr = km_gva_to_kma(arg->arg2);
   if (addr == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_connect(vcpu, arg->arg1, addr, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t bind_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
   void* addr = km_gva_to_kma(arg->arg2);
   if (addr == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_bind(vcpu, arg->arg1, addr, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t listen_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int listen(int sockfd, int backlog);
   arg->hc_ret = km_fs_listen(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t socket_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int socket(int domain, int type, int protocol);
   arg->hc_ret = km_fs_socket(vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t getsockopt_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t
   // *optlen);
   void* optval = km_gva_to_kma(arg->arg4);
   void* optlen = km_gva_to_kma(arg->arg5);
   if (optval == NULL || optlen == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_getsockopt(vcpu, arg->arg1, arg->arg2, arg->arg3, optval, optlen);
   return HC_CONTINUE;
}

static km_hc_ret_t setsockopt_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int setsockopt(int sockfd, int level, int optname, const void *optval,
   // socklen_t optlen);
   void* optval = km_gva_to_kma(arg->arg4);
   if (optval == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_setsockopt(vcpu, arg->arg1, arg->arg2, arg->arg3, optval, arg->arg5);
   return HC_CONTINUE;
}

static km_hc_ret_t sendrecvmsg_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
   // ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
   struct msghdr* msg_kma = km_gva_to_kma(arg->arg2);
   struct msghdr msg;

   if (msg_kma == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   msg.msg_name = km_gva_to_kma((uint64_t)msg_kma->msg_name);   // optional
   msg.msg_namelen = msg_kma->msg_namelen;
   msg.msg_iovlen = msg_kma->msg_iovlen;
   struct iovec iov[msg.msg_iovlen];
   for (int i = 0; i < msg.msg_iovlen; i++) {
      struct iovec* iov_kma;

      if ((iov_kma = km_gva_to_kma((uint64_t)msg_kma->msg_iov)) == NULL) {
         arg->hc_ret = -EFAULT;
         return HC_CONTINUE;
      }
      iov[i].iov_base = km_gva_to_kma((uint64_t)iov_kma[i].iov_base);
      iov[i].iov_len = iov_kma[i].iov_len;
   }
   msg.msg_iov = iov;
   msg.msg_control = km_gva_to_kma((uint64_t)msg_kma->msg_control);   // optional
   msg.msg_controllen = msg_kma->msg_controllen;
   msg.msg_flags = msg_kma->msg_flags;
   arg->hc_ret = km_fs_sendrecvmsg(vcpu, hc, arg->arg1, &msg, arg->arg3);
   if (hc == SYS_recvmsg) {
      msg_kma->msg_namelen = msg.msg_namelen;
      msg_kma->msg_controllen = msg.msg_controllen;
      msg_kma->msg_flags = msg.msg_flags;
   }
   return HC_CONTINUE;
}

static km_hc_ret_t sendfile_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
   off_t* offset = NULL;

   if (arg->arg3 != 0 && (offset = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_sendfile(vcpu, arg->arg1, arg->arg2, offset, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t copy_file_range_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
   // unsigned int flags);
   off_t* off_in = NULL;
   off_t* off_out = NULL;

   if ((arg->arg2 != 0 && (off_in = km_gva_to_kma(arg->arg2)) == NULL) ||
       (arg->arg4 != 0 && (off_out = km_gva_to_kma(arg->arg4)) == NULL)) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret =
       km_fs_copy_file_range(vcpu, arg->arg1, off_in, arg->arg3, off_out, arg->arg5, arg->arg6);
   return HC_CONTINUE;
}

static km_hc_ret_t ioctl_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int ioctl(int fd, unsigned long request, void *arg);
   // arg->hc_ret = __syscall_3(hc, arg->arg1, arg->arg2, km_gva_to_kml(arg->arg3));
   void* argp = km_gva_to_kma(arg->arg3);
   if (argp == NULL && arg->arg3 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_ioctl(vcpu, arg->arg1, arg->arg2, argp);
   return HC_CONTINUE;
}

static km_hc_ret_t fcntl_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fcntl(int fd, int cmd, ...); (Only 1 optional argument)
   arg->hc_ret = km_fs_fcntl(vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t stat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int stat(const char *pathname, struct stat *statbuf);
   void* pathname = km_gva_to_kma(arg->arg1);
   void* statbuf = km_gva_to_kma(arg->arg2);
   if (pathname == NULL || statbuf == NULL) {
      km_info(KM_TRACE_HC, "stat: FAILED path=%p buf=%p", pathname, statbuf);

      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_stat(vcpu, pathname, statbuf);
   km_info(KM_TRACE_HC, "stat: '%s'", (char*)pathname);
   return HC_CONTINUE;
}
static km_hc_ret_t lstat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int lstat(const char *pathname, struct stat *statbuf);
   void* pathname = km_gva_to_kma(arg->arg1);
   void* statbuf = km_gva_to_kma(arg->arg2);
   if (pathname == NULL || statbuf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_lstat(vcpu, pathname, statbuf);
   return HC_CONTINUE;
}

static km_hc_ret_t statx_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf);
   void* pathname = km_gva_to_kma(arg->arg2);
   struct statx* statbuf = km_gva_to_kma(arg->arg5);
   if (pathname == NULL || statbuf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_statx(vcpu, arg->arg1, pathname, arg->arg3, arg->arg4, statbuf);
   return HC_CONTINUE;
}

static km_hc_ret_t fstat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fstat(int fd, struct stat *statbuf);
   void* statbuf = km_gva_to_kma(arg->arg2);
   if (statbuf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_fstat(vcpu, arg->arg1, statbuf);
   return HC_CONTINUE;
}

static km_hc_ret_t getdirents_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
   void* dirp = km_gva_to_kma(arg->arg2);
   if (dirp == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_getdents64(vcpu, arg->arg1, dirp, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t getcwd_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getcwd(char *buf, size_t size);
   void* buf = km_gva_to_kma(arg->arg1);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_getcwd(vcpu, buf, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t close_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // arg->hc_ret = __syscall_1(hc, arg->arg1);
   arg->hc_ret = km_fs_close(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t shutdown_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int shutdown(int sockfd, int how);
   arg->hc_ret = km_fs_shutdown(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t brk_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = km_mem_brk(arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t futex_hcall(void* vcpu, int hc, km_hc_args_t* arg)
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

static km_hc_ret_t mmap_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
   arg->hc_ret = km_guest_mmap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return HC_CONTINUE;
};

static km_hc_ret_t munmap_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int munmap(void *addr, size_t length);
   arg->hc_ret = km_guest_munmap(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
};

static km_hc_ret_t mremap_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /* void
   // *new_address */);
   if (km_gva_to_kma(arg->arg1) == NULL) {
      arg->hc_ret = -EINVAL;
   } else {
      arg->hc_ret = km_guest_mremap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5);
   }
   return HC_CONTINUE;
};

static km_hc_ret_t mprotect_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   //  int mprotect(void *addr, size_t len, int prot);
   arg->hc_ret = km_guest_mprotect(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
};

static km_hc_ret_t clock_time_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int clock_gettime(clockid_t clk_id, struct timespec *tp);
   if (arg->arg2 != 0 && km_gva_to_kml(arg->arg2) == 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_2(hc, arg->arg1, km_gva_to_kml(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t madvise_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int madvise(void* addr, size_t length, int advice);
   km_infox(KM_TRACE_HC, "hc = %d (madvise), %ld %lx %lx", hc, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = km_guest_madvise(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t msync_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int msync(void *addr, size_t length, int flags);
   km_infox(KM_TRACE_HC, "hc = %d (msync), %ld %lx %lx", hc, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = km_guest_msync(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t umask_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // mode_t umask(mode_t mask);
   arg->hc_ret = __syscall_1(hc, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t readlink_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
   km_kma_t pathname = km_gva_to_kma(arg->arg1);
   km_kma_t buf = km_gva_to_kma(arg->arg2);
   if (pathname == NULL || buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_readlink(vcpu, pathname, buf, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t readlinkat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
   km_kma_t pathname = km_gva_to_kma(arg->arg2);
   km_kma_t buf = km_gva_to_kma(arg->arg3);
   if (pathname == NULL || buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_readlinkat(vcpu, arg->arg1, pathname, buf, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t getrandom_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
   void* buf = km_gva_to_kma(arg->arg1);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_3(hc, km_gva_to_kml(arg->arg1), arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t open_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int open(const char *pathname, int flags, mode_t mode);
   // arg->hc_ret = __syscall_3(hc, km_gva_to_kml(arg->arg1), arg->arg2, arg->arg3);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // TODO - check with readlink_proc_self_exe
   arg->hc_ret = km_fs_open(vcpu, pathname, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t openat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int openat(int dirfd, const char *pathname, int flags);
   // int openat(int dirfd, const char* pathname, int flags, mode_t mode);
   void* pathname = km_gva_to_kma(arg->arg2);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // TODO - check with readlink_proc_self_exe
   arg->hc_ret = km_fs_openat(vcpu, arg->arg1, pathname, arg->arg3, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t lseek_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // off_t lseek(int fd, off_t offset, int whence);
   arg->hc_ret = km_fs_lseek(vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t symlink_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int symlink(const char *target, const char *linkpath);
   void* target = km_gva_to_kma(arg->arg1);
   void* linkpath = km_gva_to_kma(arg->arg2);
   if (target == NULL || linkpath == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = hc == SYS_symlink ? km_fs_symlink(vcpu, target, linkpath)
                                   : km_fs_link(vcpu, target, linkpath);
   return HC_CONTINUE;
}

static km_hc_ret_t rename_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rename(const char *oldpath, const char *newpath);
   void* oldpath = km_gva_to_kma(arg->arg1);
   void* newpath = km_gva_to_kma(arg->arg2);
   if (oldpath == NULL || newpath == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_rename(vcpu, oldpath, newpath);
   return HC_CONTINUE;
}

static km_hc_ret_t chdir_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int chdir(const char *path);
   void* path = km_gva_to_kma(arg->arg1);
   if (path == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_chdir(vcpu, path);
   return HC_CONTINUE;
}

static km_hc_ret_t fchdir_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fchdir(int fd);
   arg->hc_ret = km_fs_fchdir(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t truncate_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int truncate(const char *path, off_t length);
   void* path = km_gva_to_kma(arg->arg1);
   if (path == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_truncate(vcpu, path, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t ftruncate_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int ftruncate(int fd, off_t length);
   arg->hc_ret = km_fs_ftruncate(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t fsync_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fsync(int fd);
   arg->hc_ret = km_fs_fsync(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t fdatasync_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fdatasync(int fd);
   arg->hc_ret = km_fs_fdatasync(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t mkdir_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int mkdir(const char *path, mode_t mode);
   void* path = km_gva_to_kma(arg->arg1);
   if (path == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_mkdir(vcpu, path, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t rmdir_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rmdir(const char *path);
   void* path = km_gva_to_kma(arg->arg1);
   if (path == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_rmdir(vcpu, path);
   return HC_CONTINUE;
}

static km_hc_ret_t select_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   //  int select(int nfds, fd_set *readfds, fd_set *writefds,
   //             fd_set *exceptfds, struct timeval *timeout);
   // NULL is a legal value for readfds, writefds, exceptfds, and timeout
   void* readfds = km_gva_to_kma(arg->arg2);
   void* writefds = km_gva_to_kma(arg->arg3);
   void* exceptfds = km_gva_to_kma(arg->arg4);
   void* timeout = km_gva_to_kma(arg->arg5);
   if (readfds == NULL && arg->arg2 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (writefds == NULL && arg->arg3 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (exceptfds == NULL && arg->arg4 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (timeout == NULL && arg->arg5 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_select(vcpu, arg->arg1, readfds, writefds, exceptfds, timeout);
   return HC_CONTINUE;
}

static km_hc_ret_t sendto_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
   //                const struct sockaddr *dest_addr, socklen_t addrlen);
   arg->hc_ret = km_fs_sendto(vcpu,
                              arg->arg1,
                              km_gva_to_kma(arg->arg2),
                              arg->arg3,
                              arg->arg4,
                              km_gva_to_kma(arg->arg5),
                              arg->arg6);
   return HC_CONTINUE;
}

static km_hc_ret_t nanosleep_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int nanosleep(const struct timespec* req, struct timespec* rem);
   // NULL is a legal value for rem.
   void* rem = km_gva_to_kma(arg->arg2);
   if (rem == NULL && arg->arg2 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_2(hc, km_gva_to_kml(arg->arg1), km_gva_to_kml(arg->arg2));
   return HC_CONTINUE;
}

static km_hc_ret_t access_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int access(const char *pathname, int mode);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }

   arg->hc_ret = km_fs_access(vcpu, pathname, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t dup_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int dup(int oldfd);
   arg->hc_ret = km_fs_dup(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t dup2_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int dup(int oldfd);
   arg->hc_ret = km_fs_dup2(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t dup3_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int dup3(int oldfd, int newfd, int flags);
   arg->hc_ret = km_fs_dup3(vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t pause_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int pause(void);
   arg->hc_ret = __syscall_0(hc);
   return HC_CONTINUE;
}

static km_hc_ret_t get_sock_peer_name_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   // int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   void* addr = km_gva_to_kma(arg->arg2);
   void* addrlen = km_gva_to_kma(arg->arg3);
   if (addr == NULL || addrlen == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_get_sock_peer_name(vcpu, hc, arg->arg1, addr, addrlen);
   return HC_CONTINUE;
}

static km_hc_ret_t poll_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int poll(struct pollfd *fds, nfds_t nfds, int timeout);
   void* fds = km_gva_to_kma(arg->arg1);
   if (fds == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_poll(vcpu, fds, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t accept4_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int accept4(int sockfd, struct sockaddr *addr,
   //             socklen_t *addrlen, int flags);
   // NULL is a legal value for addr and addrlen.
   void* addr = km_gva_to_kma(arg->arg2);
   void* addrlen = km_gva_to_kma(arg->arg3);
   if (addr == NULL && arg->arg2 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (addrlen == NULL && arg->arg3 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_accept4(vcpu, arg->arg1, addr, addrlen, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t recvfrom_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
   //                  struct sockaddr *srcaddr, socklen_t *addrlen);
   // srcaddr and addrlen are optional
   void* buf = km_gva_to_kma(arg->arg2);
   void* srcaddr = km_gva_to_kma(arg->arg5);
   void* addrlen = km_gva_to_kma(arg->arg6);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (srcaddr == NULL && arg->arg5 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (addrlen == NULL && arg->arg6 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_recvfrom(vcpu, arg->arg1, buf, arg->arg3, arg->arg4, srcaddr, addrlen);
   return HC_CONTINUE;
}

static km_hc_ret_t getrusage_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getrusage(int who, struct rusage *usage);
   void* buf = km_gva_to_kma(arg->arg2);
   if (buf == NULL) {
      arg->hc_ret = -EFAULT;
   } else {
      arg->hc_ret = 0;
      memset(buf, 0, sizeof(struct rusage));
   }
   return HC_CONTINUE;
}

static km_hc_ret_t dummy_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = 0;
   km_infox(KM_TRACE_HC,
            "hc = %d (dummy for %s), %ld %lx %lx %lx %lx",
            hc,
            km_hc_name_get(hc),
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4,
            arg->arg5);
   return HC_CONTINUE;
}

static km_hc_ret_t getpid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // pid_t getpid(void);
   arg->hc_ret = machine.pid;
   return HC_CONTINUE;
}

static km_hc_ret_t getppid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // pid_t getppid(void);
   arg->hc_ret = machine.ppid;
   return HC_CONTINUE;
}

static km_hc_ret_t gettid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // pid_t gettid(void);
   arg->hc_ret = km_vcpu_get_tid(vcpu);
   return HC_CONTINUE;
}

static km_hc_ret_t guest_interrupt_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   km_handle_interrupt(vcpu);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigprocmask_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rt_sigprocmask(int how, const kernel_sigset_t *set, kernel_sigset_t *oldset, size_t sigsetsize);
   km_sigset_t* set = NULL;
   km_sigset_t* oldset = NULL;

   if (arg->arg2 != 0 && (set = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg3 != 0 && (oldset = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigprocmask(vcpu, arg->arg1, set, oldset, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigaction_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
   km_sigaction_t* act = NULL;
   km_sigaction_t* oldact = NULL;

   if (arg->arg2 != 0 && (act = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg3 != 0 && (oldact = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigaction(vcpu, arg->arg1, act, oldact, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigreturn_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   km_rt_sigreturn(vcpu);   // don't care about arg or return code.
   return HC_CONTINUE;
}

static km_hc_ret_t sigaltstack_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int sigaltstack(const stack_t *ss, stack_t *old_ss);
   km_stack_t* old = NULL;
   km_stack_t* new = NULL;

   if (arg->arg1 != 0 && (new = km_gva_to_kma(arg->arg1)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg2 != 0 && (old = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_sigaltstack(vcpu, new, old);
   return HC_CONTINUE;
}

static km_hc_ret_t kill_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int kill(pid_t pid, int sig)
   arg->hc_ret = km_kill(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t tgkill_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int tgkill(int tgid, int tid, int sig);
   km_infox(KM_TRACE_HC, "Ignoring tgid %ld", arg->arg1);
   arg->hc_ret = km_tkill(vcpu, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t tkill_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int tkill(pid_t tid, int sig)
   // From man(3): "tkill() is an obsolete predecessor to tgkill().  It allows only the target
   // thread ID to be specified, which may result in the wrong thread being signaled if a thread
   // terminates and its thread  ID is recycled.Avoid using this system call"
   km_infox(KM_TRACE_HC, "tkill usage is not recommended, %ld can be reused", arg->arg1);
   arg->hc_ret = km_tkill(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigpending_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rt_sigpending(sigset_t *set, size_t sigsetsize)
   void* sigset = km_gva_to_kma(arg->arg1);
   if (sigset == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigpending(vcpu, sigset, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t rt_sigsuspend_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rt_sigsuspend(const sigset_t *mask)
   km_sigset_t* mask = NULL;
   if (arg->arg1 != 0 && (mask = km_gva_to_kma(arg->arg1)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigsuspend(vcpu, mask, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll1_create_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int epoll_create1(int flags);
   arg->hc_ret = km_fs_epoll_create1(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll_ctl_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
   // event can be NULL
   void* event = km_gva_to_kma(arg->arg4);
   if (event == NULL && arg->arg4 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_epoll_ctl(vcpu, arg->arg1, arg->arg2, arg->arg3, event);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll_pwait_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const
   // sigset_t *sigmask, size_t sigmasklen);
   void* events = km_gva_to_kma(arg->arg2);
   void* sigmask = km_gva_to_kma(arg->arg5);
   if (events == NULL || (sigmask == NULL && arg->arg5 != 0)) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_epoll_pwait(vcpu, arg->arg1, events, arg->arg3, arg->arg4, sigmask, arg->arg6);
   return HC_CONTINUE;
}

static km_hc_ret_t pipe_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int pipe2(int pipefd[2], int flags);
   void* pipefd = km_gva_to_kma(arg->arg1);
   if (pipefd == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_pipe(vcpu, pipefd);
   return HC_CONTINUE;
}

static km_hc_ret_t pipe2_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   void* pipefd = km_gva_to_kma(arg->arg1);
   if (pipefd == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // int pipe2(int pipefd[2], int flags);
   arg->hc_ret = km_fs_pipe2(vcpu, pipefd, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t eventfd2_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int eventfd2(unsigned int initval, int flags);
   arg->hc_ret = km_fs_eventfd2(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t prlimit64_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit
   // *old_limit);
   // newlimit and oldlimit are optional
   void* newlimit = km_gva_to_kma(arg->arg3);
   void* oldlimit = km_gva_to_kma(arg->arg4);
   if (newlimit == NULL && arg->arg3 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (oldlimit == NULL && arg->arg4 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_prlimit64(vcpu, arg->arg1, arg->arg2, newlimit, oldlimit);
   return HC_CONTINUE;
}

static km_hc_ret_t unlink_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int unlink(char *pathname);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_unlink(vcpu, pathname);
   return HC_CONTINUE;
}

static km_hc_ret_t mknod_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int mknod(const char *pathname, mode_t mode, dev_t dev);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_mknod(vcpu, pathname, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t chown_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int chown(const char *pathname, uid_t owner, gid_t group);
   // int lchown(const char *pathname, uid_t owner, gid_t group);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = hc == SYS_chown ? km_fs_chown(vcpu, pathname, arg->arg2, arg->arg3)
                                 : km_fs_lchown(vcpu, pathname, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t fchown_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fchown(int fd, uid_t owner, gid_t group);
   arg->hc_ret = km_fs_fchown(vcpu, arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t chmod_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int chmod(const char *pathname, mode_t mode);
   void* pathname = km_gva_to_kma(arg->arg1);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_chmod(vcpu, pathname, arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t fchmod_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int fchmod(int fd, mode_t mode);
   arg->hc_ret = km_fs_fchmod(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
}

/*
 * musl c library seems to use the fork system call for the fork() function.
 * glibc uses the clone() system call to perform forks.  And it uses the following arguments:
 *  ret = INLINE_SYSCALL_CALL (clone, flags, 0, NULL, ctid, 0)
 * so the code is written to support this.
 */
static int km_clone_process(km_vcpu_t* vcpu, km_hc_args_t* arg)
{
   int rc;
   if (arg->arg4 != 0 && km_gva_to_kma(arg->arg4) == NULL) {
      rc = -EFAULT;
   } else {
      // Advance rip beyond the hypercall out instruction
      km_vcpu_sync_rip(vcpu);
      rc = km_before_fork(vcpu, arg, 1);
   }
   return rc;
}

static km_hc_ret_t clone_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // Raw clone system call signature for x86_64 is:
   // long clone(unsigned long flags, void* child_stack, int* ptid, int* ctid, unsigned long newtls);
   if ((arg->arg1 & CLONE_THREAD) == 0) {
      arg->hc_ret = km_clone_process(vcpu, arg);
      return arg->hc_ret == 0 ? HC_DOFORK : HC_CONTINUE;
   }
   arg->hc_ret = km_clone(vcpu, arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5);
   return HC_CONTINUE;
}

static km_hc_ret_t set_tid_address_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = km_set_tid_address(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t unmapself_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // pthread_exit() does this at the end to unmap the stack and exit:
   // _Noreturn void__unmapself(void* base, size_t size);
   (void)km_guest_munmap(vcpu, arg->arg1, arg->arg2);
   return HC_STOP;
}

static km_hc_ret_t sched_getaffinity_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int sched_getaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
   km_infox(KM_TRACE_SCHED, "%s(0x%lx, 0x%lx, 0x%lx)", __FUNCTION__, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = __syscall_3(hc, arg->arg1, arg->arg2, km_gva_to_kml(arg->arg3));
   return HC_CONTINUE;
}

static km_hc_ret_t sched_setaffinity_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
   km_infox(KM_TRACE_SCHED, "%s(0x%lx, 0x%lx, 0x%lx)", __FUNCTION__, arg->arg1, arg->arg2, arg->arg3);
   warnx("%s - Unsupported", __FUNCTION__);
   arg->hc_ret = -ENOTSUP;
   return HC_CONTINUE;
}

static km_hc_ret_t getcpu_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
   km_infox(KM_TRACE_SCHED, "%s(0x%lx, 0x%lx, 0x%lx)", __FUNCTION__, arg->arg1, arg->arg2, arg->arg3);
   uint64_t* cpu = NULL;
   if (arg->arg1 != 0 && (cpu = km_gva_to_kma(arg->arg1)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   uint64_t* node = NULL;
   if (arg->arg2 != 0 && (node = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // arg->arg3 (tchache) is unused and ignored. See 'NOTES' in 'man 2 getcpu'.

   if (cpu != NULL) {
      *cpu = 0;
   }
   if (node != NULL) {
      *node = 0;
   }
   arg->hc_ret = 0;
   return HC_CONTINUE;
}

static km_hc_ret_t sysinfo_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int sysinfo(struct sysinfo *info)
   km_infox(KM_TRACE_SCHED, "%s(0x%lx)", __FUNCTION__, arg->arg1);
   struct sysinfo* si = km_gva_to_kma(arg->arg1);
   if (si == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_1(hc, km_gva_to_kml(arg->arg1));
   if (arg->hc_ret == 0) {
      si->procs = 1;
   }
   return HC_CONTINUE;
}

static km_hc_ret_t times_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // clock_t times(struct tms *buf);
   struct tms* t = km_gva_to_kma(arg->arg1);
   if (t == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   clock_t ret = times(t);
   if (ret == (clock_t)-1) {
      arg->hc_ret = -errno;
      return HC_CONTINUE;
   }
   arg->hc_ret = ret;
   return HC_CONTINUE;
}

static km_hc_ret_t getpgrp_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // pid_t getpgrp(void);
   arg->hc_ret = 1;
   return HC_CONTINUE;
}

static km_hc_ret_t uname_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int uname(struct utsname *name);
   struct utsname* name;
   if (arg->arg1 == 0 || (name = km_gva_to_kma(arg->arg1)) == NULL ||
       km_gva_to_kma(arg->arg1 + sizeof(*name) - 1) == NULL) {
      arg->hc_ret = -1;
      return HC_CONTINUE;
   }

   arg->hc_ret = uname(name);
   // Overwrite Kontain specific info. Buffers 65 bytes each, hardcoded in musl, so we are good
   strcpy(name->sysname, "kontain-runtime");
   // used by some libs (eg. libc) as kernel version check, numbers too low (e.g. 0.8)
   // may cause "kernel too old" failures.
   strcpy(name->release, "4.1");
   strcpy(name->version, "preview");
   strcpy(name->machine, "kontain_VM");
   return HC_CONTINUE;
}

static int do_exec(char* filename, char** argv, char** envp)
{
   char** newenv;
   char** newargv;

   // Add some km state to the environment.
   newenv = km_exec_build_env(envp);
   if (newenv == NULL) {
      return -ENOMEM;
   }
   // Build argv line with km program and args before the payload's args.
   newargv = km_exec_build_argv(filename, argv);
   if (newargv == NULL) {
      free(newenv);
      return -ENOMEM;
   }

   // Start km again with the new payload program
   execve(km_get_self_name(), newargv, newenv);
   // If we are here, execve() failed.  So we need to cleanup.
   km_info(KM_TRACE_HC, "execve failed, error %d", errno);
   free(newargv);
   free(newenv);
   return -errno;
}

/*
 * int execve(const char *filename, char *const argv[], char *const envp[]);
 */
static km_hc_ret_t execve_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   char* filename = km_gva_to_kma(arg->arg1);
   char** argv = km_gva_to_kma(arg->arg2);
   char** envp = km_gva_to_kma(arg->arg3);

   if (filename == NULL || argv == NULL || envp == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }

   arg->hc_ret = do_exec(filename, argv, envp);
   return HC_CONTINUE;
}

static int execveat_openexe(int dirfd, char* pathname, int flag, int open_flag)
{
   int exefd;
   if ((flag & AT_EMPTY_PATH) != 0) {
      exefd = dirfd;
   } else if (dirfd == AT_FDCWD || *pathname == '/') {
      exefd = open(pathname, open_flag);
   } else {
      exefd = openat(dirfd, pathname, open_flag);
   }
   if (exefd < 0) {
      exefd = -errno;
   }
   return exefd;
}

/*
 * int execveat(int dirfd, const char *pathname,
 *              char *const argv[], char *const envp[],
 *              int flags);
 * flags can be AT_EMPTY_PATH and AT_SYMLINK_NOFOLLOW
 *
 * Note that fexecve() is implemented as a subset of execveat() so this
 * hcall covers the both
 */
static km_hc_ret_t execveat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   int dirfd = AT_FDCWD;
   char* pathname = NULL;
   char** argv = km_gva_to_kma(arg->arg3);
   char** envp = km_gva_to_kma(arg->arg4);
   int open_flag = O_RDONLY | ((arg->arg5 & AT_SYMLINK_NOFOLLOW) != 0 ? O_NOFOLLOW : 0);

   // Validate the arguments.
   if (arg->arg1 != AT_FDCWD && (dirfd = km_fs_g2h_fd(arg->arg1)) < 0) {
      arg->hc_ret = -EBADF;
      return HC_CONTINUE;
   }
   if (arg->arg2 != 0 && (pathname = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   if (argv == NULL || envp == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (dirfd == AT_FDCWD && ((arg->arg5 & AT_EMPTY_PATH) != 0 || pathname == NULL)) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }

   // Resolve dirfd and pathname to a host fd open
   int exefd = execveat_openexe(dirfd, pathname, arg->arg5, open_flag);
   if (exefd < 0) {
      arg->hc_ret = exefd;
      return HC_CONTINUE;
   }

   // Get the filename for the open km payload executable.
   char* exe_path = km_guestfd_name(vcpu, km_fs_h2g_fd(exefd));
   assert(exe_path != NULL);
   if (exefd != dirfd) {
      close(exefd);
   }

   arg->hc_ret = do_exec(exe_path, argv, envp);
   return HC_CONTINUE;
}

static km_hc_ret_t fork_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // Advance rip beyond the hypercall out instruction
   km_vcpu_sync_rip(vcpu);

   // Save this thread's vcpu state for the child process And, serialize concurrent forks.
   int rc = km_before_fork(vcpu, arg, 0);
   arg->hc_ret = rc;
   return rc == 0 ? HC_DOFORK : HC_CONTINUE;
}

/*
 * pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
 */
static km_hc_ret_t wait4_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   pid_t linux_pid;
   pid_t wait4_rv;

   km_infox(KM_TRACE_VCPU,
            "pid %ld, wstatus 0x%lx, options 0x%lx, rusage 0x%lx",
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4);

   if ((int64_t)arg->arg1 < -1 || arg->arg1 == 0) {
      assert("no process group support yet" == NULL);
   }
   if (arg->arg1 == -1) {
      linux_pid = 0;
   } else {
      linux_pid = km_pid_xlate_kpid(arg->arg1);
   }

   km_infox(KM_TRACE_HC, "waiting for km pid %ld, linux pid %d", arg->arg1, linux_pid);
   wait4_rv = wait4(linux_pid,
                    (int*)km_gva_to_kma(arg->arg2),
                    (int)arg->arg3,
                    (struct rusage*)km_gva_to_kma(arg->arg4));
   km_infox(KM_TRACE_HC, "wait4 returns %d, errno %d", wait4_rv, errno);
   if (wait4_rv < 0) {
      arg->hc_ret = -errno;
   } else {
      if (wait4_rv != 0) {
         arg->hc_ret = km_pid_xlate_lpid(wait4_rv);
         km_pid_free(arg->hc_ret);
      } else {
         arg->hc_ret = 0;
      }
   }
   km_infox(KM_TRACE_HC, "hc_ret %ld", arg->hc_ret);
   return HC_CONTINUE;
}

/*
 * int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
 */
static km_hc_ret_t waitid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   siginfo_t* sip = km_gva_to_kma(arg->arg3);
   pid_t linux_pid = 0;

   if (sip == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }

   switch ((idtype_t)arg->arg1) {
      case P_PID:
         linux_pid = km_pid_xlate_kpid(arg->arg2);
         break;
      case P_ALL:
         linux_pid = 0;
         break;
      case P_PGID:
         errx(2, "Can't wait for process group id yet");
         break;
      default:
         errx(2, "Unknown idtype %lu", arg->arg1);
         break;
   }

   km_infox(KM_TRACE_HC,
            "waiting for km pid %lu, linux pid %d, options 0x%lx",
            arg->arg2,
            linux_pid,
            arg->arg4);
   sip->si_pid = 0;
   if (waitid((idtype_t)arg->arg1, linux_pid, sip, (int)arg->arg4) < 0) {
      arg->hc_ret = -errno;
   } else {
      // Return the km pid converted from the linux pid.
      if (sip->si_pid != 0) {
         sip->si_pid = km_pid_xlate_lpid(sip->si_pid);
         km_pid_free(sip->si_pid);
      }
      km_infox(KM_TRACE_HC, "returns si_pid %d", sip->si_pid);
      arg->hc_ret = 0;
   }
   return HC_CONTINUE;
}

/*
 * int setitimer(int which, const struct itimerval *new_value,
 *               struct itimerval *old_value);
 */
static km_hc_ret_t setitimer_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   struct itimerval* old = NULL;
   struct itimerval* new = km_gva_to_kma(arg->arg2);
   if (new == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg3 != 0) {
      if ((old = km_gva_to_kma(arg->arg3)) == NULL) {
         arg->hc_ret = -EFAULT;
         return HC_CONTINUE;
      }
   }
   arg->hc_ret = setitimer(arg->arg1, new, old);
   return HC_CONTINUE;
}

/*
 * int getitimer(int which, struct itimerval *curr_value);
 */
static km_hc_ret_t getitimer_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   struct itimerval* curr = km_gva_to_kma(arg->arg2);
   if (curr == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = getitimer(arg->arg1, curr);
   return HC_CONTINUE;
}

static km_hc_ret_t snapshot_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // TODO: Stop the world.
   warnx("SNAPSHOT");
   km_vcpu_sync_rip(vcpu);
   km_read_registers(vcpu);
   km_vcpu_pause_all();
   km_dump_core(km_get_snapshot_path(), vcpu, NULL);
   return HC_ALLSTOP;
}

km_hcall_fn_t km_hcalls_table[KM_MAX_HCALL];
km_hc_stats_t* km_hcalls_stats;

static void km_print_hcall_stats(void)
{
   for (int hc = 0; hc < KM_MAX_HCALL; hc++) {
      if (km_hcalls_stats[hc].count != 0) {
         printf("%24s(%3d) called\t %9ld times, latency usecs %9ld avg %9ld min %9ld max\n",
                km_hc_name_get(hc),
                hc,
                km_hcalls_stats[hc].count,
                km_hcalls_stats[hc].total / km_hcalls_stats[hc].count / 1000,
                km_hcalls_stats[hc].min / 1000,
                km_hcalls_stats[hc].max / 1000);
      }
   }
}

void km_hcalls_init(void)
{
   km_hcalls_table[SYS_arch_prctl] = arch_prctl_hcall;

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
   km_hcalls_table[SYS_connect] = connect_hcall;
   km_hcalls_table[SYS_socketpair] = socketpair_hcall;
   km_hcalls_table[SYS_bind] = bind_hcall;
   km_hcalls_table[SYS_listen] = listen_hcall;
   km_hcalls_table[SYS_socket] = socket_hcall;
   km_hcalls_table[SYS_getsockopt] = getsockopt_hcall;
   km_hcalls_table[SYS_setsockopt] = setsockopt_hcall;
   km_hcalls_table[SYS_sendmsg] = sendrecvmsg_hcall;
   km_hcalls_table[SYS_recvmsg] = sendrecvmsg_hcall;
   km_hcalls_table[SYS_sendfile] = sendfile_hcall;
   km_hcalls_table[SYS_copy_file_range] = copy_file_range_hcall;
   km_hcalls_table[SYS_ioctl] = ioctl_hcall;
   km_hcalls_table[SYS_fcntl] = fcntl_hcall;
   km_hcalls_table[SYS_stat] = stat_hcall;
   km_hcalls_table[SYS_lstat] = lstat_hcall;
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
   km_hcalls_table[SYS_clock_gettime] = clock_time_hcall;
   km_hcalls_table[SYS_clock_getres] = clock_time_hcall;
   km_hcalls_table[SYS_clock_settime] = clock_time_hcall;
   km_hcalls_table[SYS_madvise] = madvise_hcall;
   km_hcalls_table[SYS_msync] = msync_hcall;

   km_hcalls_table[SYS_umask] = umask_hcall;
   km_hcalls_table[SYS_readlink] = readlink_hcall;
   km_hcalls_table[SYS_readlinkat] = readlinkat_hcall;
   km_hcalls_table[SYS_getrandom] = getrandom_hcall;
   km_hcalls_table[SYS_open] = open_hcall;
   km_hcalls_table[SYS_openat] = openat_hcall;
   km_hcalls_table[SYS_lseek] = lseek_hcall;
   km_hcalls_table[SYS_rename] = rename_hcall;
   km_hcalls_table[SYS_link] = symlink_hcall;
   km_hcalls_table[SYS_symlink] = symlink_hcall;
   km_hcalls_table[SYS_mkdir] = mkdir_hcall;
   km_hcalls_table[SYS_rmdir] = rmdir_hcall;
   km_hcalls_table[SYS_unlink] = unlink_hcall;
   km_hcalls_table[SYS_mknod] = mknod_hcall;
   km_hcalls_table[SYS_chown] = chown_hcall;
   km_hcalls_table[SYS_lchown] = chown_hcall;
   km_hcalls_table[SYS_fchown] = fchown_hcall;
   km_hcalls_table[SYS_chmod] = chmod_hcall;
   km_hcalls_table[SYS_fchmod] = fchmod_hcall;
   km_hcalls_table[SYS_chdir] = chdir_hcall;
   km_hcalls_table[SYS_fchdir] = fchdir_hcall;
   km_hcalls_table[SYS_truncate] = truncate_hcall;
   km_hcalls_table[SYS_ftruncate] = ftruncate_hcall;
   km_hcalls_table[SYS_fsync] = fsync_hcall;
   km_hcalls_table[SYS_fdatasync] = fdatasync_hcall;
   km_hcalls_table[SYS_select] = select_hcall;
   km_hcalls_table[SYS_pause] = pause_hcall;
   km_hcalls_table[SYS_sendto] = sendto_hcall;
   km_hcalls_table[SYS_nanosleep] = nanosleep_hcall;
   km_hcalls_table[SYS_getsockname] = get_sock_peer_name_hcall;
   km_hcalls_table[SYS_getpeername] = get_sock_peer_name_hcall;
   km_hcalls_table[SYS_poll] = poll_hcall;
   km_hcalls_table[SYS_accept4] = accept4_hcall;
   km_hcalls_table[SYS_recvfrom] = recvfrom_hcall;
   km_hcalls_table[SYS_epoll_create1] = epoll1_create_hcall;
   km_hcalls_table[SYS_epoll_ctl] = epoll_ctl_hcall;
   km_hcalls_table[SYS_epoll_pwait] = epoll_pwait_hcall;
   km_hcalls_table[SYS_access] = access_hcall;
   km_hcalls_table[SYS_dup] = dup_hcall;
   km_hcalls_table[SYS_dup2] = dup2_hcall;
   km_hcalls_table[SYS_dup3] = dup3_hcall;
   km_hcalls_table[SYS_pipe] = pipe_hcall;
   km_hcalls_table[SYS_pipe2] = pipe2_hcall;
   km_hcalls_table[SYS_eventfd2] = eventfd2_hcall;
   km_hcalls_table[SYS_prlimit64] = prlimit64_hcall;

   km_hcalls_table[SYS_rt_sigprocmask] = rt_sigprocmask_hcall;
   km_hcalls_table[SYS_rt_sigaction] = rt_sigaction_hcall;
   km_hcalls_table[SYS_rt_sigreturn] = rt_sigreturn_hcall;
   km_hcalls_table[SYS_rt_sigpending] = rt_sigpending_hcall;
   km_hcalls_table[SYS_rt_sigsuspend] = rt_sigsuspend_hcall;
   km_hcalls_table[SYS_sigaltstack] = sigaltstack_hcall;
   km_hcalls_table[SYS_kill] = kill_hcall;
   km_hcalls_table[SYS_tkill] = tkill_hcall;
   km_hcalls_table[SYS_tgkill] = tgkill_hcall;

   km_hcalls_table[SYS_getpid] = getpid_hcall;
   km_hcalls_table[SYS_getppid] = getppid_hcall;
   km_hcalls_table[SYS_gettid] = gettid_hcall;

   km_hcalls_table[SYS_getrusage] = getrusage_hcall;
   km_hcalls_table[SYS_geteuid] = dummy_hcall;
   km_hcalls_table[SYS_getuid] = dummy_hcall;
   km_hcalls_table[SYS_getegid] = dummy_hcall;
   km_hcalls_table[SYS_getgid] = dummy_hcall;
   km_hcalls_table[SYS_sched_yield] = dummy_hcall;
   km_hcalls_table[SYS_setpriority] = dummy_hcall;
   km_hcalls_table[SYS_sched_getaffinity] = sched_getaffinity_hcall;
   km_hcalls_table[SYS_sched_setaffinity] = sched_setaffinity_hcall;
   km_hcalls_table[SYS_prctl] = dummy_hcall;

   km_hcalls_table[SYS_clone] = clone_hcall;
   km_hcalls_table[SYS_set_tid_address] = set_tid_address_hcall;
   km_hcalls_table[SYS_membarrier] = dummy_hcall;
   km_hcalls_table[SYS_getcpu] = getcpu_hcall;
   km_hcalls_table[SYS_sysinfo] = sysinfo_hcall;

   km_hcalls_table[SYS_execve] = execve_hcall;
   km_hcalls_table[SYS_execveat] = execveat_hcall;
   km_hcalls_table[SYS_fork] = fork_hcall;
   km_hcalls_table[SYS_wait4] = wait4_hcall;
   km_hcalls_table[SYS_waitid] = waitid_hcall;
   km_hcalls_table[SYS_times] = times_hcall;
   km_hcalls_table[SYS_getpgrp] = getpgrp_hcall;

   km_hcalls_table[SYS_uname] = uname_hcall;

   km_hcalls_table[SYS_setitimer] = setitimer_hcall;
   km_hcalls_table[SYS_getitimer] = getitimer_hcall;

   km_hcalls_table[HC_guest_interrupt] = guest_interrupt_hcall;
   km_hcalls_table[HC_unmapself] = unmapself_hcall;
   km_hcalls_table[HC_snapshot] = snapshot_hcall;

   if (km_collect_hc_stats == 1) {
      if ((km_hcalls_stats = calloc(KM_MAX_HCALL, sizeof(km_hc_stats_t))) == NULL) {
         err(1, "KVM: no memory for hcall stats");
      }
      for (int i = 0; i < KM_MAX_HCALL; i++) {
         km_hcalls_stats[i].min = UINT64_MAX;
      }
   }
}

void km_hcalls_fini(void)
{
   if (km_collect_hc_stats == 1) {
      km_print_hcall_stats();
   }
}
