/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#include "km_guest.h"
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
            km_write_xcrs(vcpu);
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
   if (buf == NULL && arg->arg3 != 0) {
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
   km_infox(KM_TRACE_HC, "ioctl arg1 0x%lx, arg2 0x%lx, arg3 0x%lx", arg->arg1, arg->arg2, arg->arg3);
   void* argp = km_gva_to_kma(arg->arg3);
   if (argp == NULL && arg->arg3 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg2 == TIOCSPGRP) {
      km_infox(KM_TRACE_HC, "TIOCSPGRP pgid %d", argp != NULL ? *(pid_t*)argp : -1);
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

// int getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
static km_hc_ret_t getdents_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   void* dirp = km_gva_to_kma(arg->arg2);
   if (dirp == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_getdents(vcpu, arg->arg1, dirp, arg->arg3);
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
   arg->hc_ret = km_fs_close(vcpu, arg->arg1);
   return HC_CONTINUE;
}

static km_hc_ret_t flock_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int flock(int fd, int operation);
   arg->hc_ret = km_fs_flock(vcpu, arg->arg1, arg->arg2);
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
   km_infox(KM_TRACE_HC,
            "hc = %d (mmap), 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx",
            hc,
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4,
            arg->arg5,
            arg->arg6);
   arg->hc_ret = km_guest_mmap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return HC_CONTINUE;
};

static km_hc_ret_t munmap_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int munmap(void *addr, size_t length);
   km_infox(KM_TRACE_HC, "hc = %d (munmap), 0x%lx 0x%lx", hc, arg->arg1, arg->arg2);
   arg->hc_ret = km_guest_munmap(vcpu, arg->arg1, arg->arg2);
   return HC_CONTINUE;
};

static km_hc_ret_t mremap_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /* void
   // *new_address */);
   km_infox(KM_TRACE_HC,
            "hc = %d (mremap), 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx",
            hc,
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4,
            arg->arg5);
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
   km_infox(KM_TRACE_HC, "hc = %d (mprotect), 0x%lx 0x%lx 0x%lx", hc, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = km_guest_mprotect(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
};

static km_hc_ret_t time_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // time_t time(time_t* tloc);
   // glibc 2.31 moved away from time() syscall and uses clock_gettime(CLOCK_REALTIME)
   // We just do the same
   struct timespec ts;
   time_t* arg_t = km_gva_to_kma(arg->arg1);
   if (arg->arg1 != 0 && arg_t == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   int rc = __syscall_2(SYS_clock_gettime, CLOCK_REALTIME, (uint64_t)&ts);
   if (rc < 0) {
      arg->hc_ret = rc;
      return HC_CONTINUE;
   }
   if (arg_t != NULL) {
      *arg_t = ts.tv_sec;
   }
   arg->hc_ret = ts.tv_sec;
   return HC_CONTINUE;
}

static km_hc_ret_t gettimeofday_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int gettimeofday(struct timeval *tv, struct timezone *tz);
   if (km_gva_to_kml(arg->arg1) == 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if (arg->arg2 != 0 && km_gva_to_kml(arg->arg2) == 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_2(SYS_gettimeofday, km_gva_to_kml(arg->arg1), km_gva_to_kml(arg->arg2));
   return HC_CONTINUE;
}

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
   km_infox(KM_TRACE_HC, "hc = %d (madvise), 0x%lx 0x%lx 0x%lx", hc, arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = km_guest_madvise(arg->arg1, arg->arg2, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t msync_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int msync(void *addr, size_t length, int flags);
   km_infox(KM_TRACE_HC, "hc = %d (msync), 0x%lx 0x%lx 0x%lx", hc, arg->arg1, arg->arg2, arg->arg3);
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

static km_hc_ret_t pselect6_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   //  int pselect6(int nfds, fd_set *readfds, fd_set *writefds,
   //             fd_set *exceptfds, struct timeval *timeout, void* ptr);
   // NULL is a legal value for readfds, writefds, exceptfds, and timeout
   // See 'man pselect' man page for ptr format
   void* readfds = km_gva_to_kma(arg->arg2);
   void* writefds = km_gva_to_kma(arg->arg3);
   void* exceptfds = km_gva_to_kma(arg->arg4);
   void* timeout = km_gva_to_kma(arg->arg5);
   km_pselect6_sigmask_t* sigp = km_gva_to_kma(arg->arg6);
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
   if (sigp == NULL && arg->arg6 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   void* sav_ss = 0;
   if (sigp != NULL) {
      void* tmp = km_gva_to_kma((uintptr_t)sigp->ss);
      if (tmp == NULL) {
         arg->hc_ret = -EFAULT;
         return HC_CONTINUE;
      }
      sav_ss = sigp->ss;
      sigp->ss = tmp;
   }
   arg->hc_ret = km_fs_pselect6(vcpu, arg->arg1, readfds, writefds, exceptfds, timeout, sigp);
   if (sigp != NULL) {
      sigp->ss = sav_ss;
   }
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

static km_hc_ret_t clock_nanosleep_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int clock_nanosleep(clockid_t clock_id,
   //                     int flags,
   //                     const struct timespec* request,
   //                     struct timespec* remain);
   // NULL is a legal value for rem.
   uint64_t rem = km_gva_to_kml(arg->arg4);
   if (rem == 0 && arg->arg4 != 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   uint64_t req = km_gva_to_kml(arg->arg3);
   if (req == 0) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_4(hc, arg->arg1, arg->arg2, req, rem);
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
      arg->hc_ret = __syscall_2(hc, arg->arg1, (uint64_t)buf);
   }
   return HC_CONTINUE;
}

static km_hc_ret_t dummy_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = 0;
   km_infox(KM_TRACE_HC,
            "hc = %d (dummy for %s), 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx",
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

static km_hc_ret_t rt_sigtimedwait(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int rt_sigtimedwait(const sigset_t *set, const siginfo_t *info, const struct timespec
   // *timeout, size_t setlen);
   km_sigset_t* set = km_gva_to_kma(arg->arg1);
   siginfo_t* info = NULL;
   struct timespec* timeout = NULL;

   if ((arg->arg2 != 0 && (info = km_gva_to_kma(arg->arg2)) == NULL) ||
       (arg->arg3 != 0 && (timeout = km_gva_to_kma(arg->arg3)) == NULL)) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_rt_sigtimedwait(vcpu, set, info, timeout, arg->arg4);
   return HC_CONTINUE;
}

static km_hc_ret_t epoll1_create_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int epoll_create(int size);
   // int epoll_create1(int flags);
   arg->hc_ret = km_fs_epoll_create1(vcpu, hc == SYS_epoll_create ? 0 : arg->arg1);
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

static km_hc_ret_t epoll_wait_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
   void* events = km_gva_to_kma(arg->arg2);
   if (events == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_epoll_wait(vcpu, arg->arg1, events, arg->arg3, arg->arg4);
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

static km_hc_ret_t unlinkat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int unlinkat(int dirfd, const char *pathname, int flags);
   void* pathname = km_gva_to_kma(arg->arg2);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_unlinkat(vcpu, arg->arg1, pathname, arg->arg3);
   return HC_CONTINUE;
}

static km_hc_ret_t utimensat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   //  int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
   void* pathname = NULL;
   if (arg->arg2 != 0 && (pathname = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   struct timespec* ts = NULL;
   if (arg->arg3 != 0 && (ts = km_gva_to_kma(arg->arg3)) == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }

   arg->hc_ret = km_fs_utimensat(vcpu, arg->arg1, pathname, ts, arg->arg4);
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

static km_hc_ret_t clone3_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // long syscall(SYS_clone3, struct clone_args *cl_args, size_t size);
   struct clone_args* cl_args = km_gva_to_kma(arg->arg1);

   if (cl_args == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   if ((cl_args->flags & CLONE_THREAD) == 0) {
      arg->hc_ret = -ENOSYS;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_clone3(vcpu, cl_args);
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
   km_infox(KM_TRACE_SCHED, "(0x%lx, 0x%lx, 0x%lx)", arg->arg1, arg->arg2, arg->arg3);
   arg->hc_ret = __syscall_3(hc, arg->arg1, arg->arg2, km_gva_to_kml(arg->arg3));
   return HC_CONTINUE;
}

static km_hc_ret_t sched_setaffinity_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
   km_infox(KM_TRACE_SCHED, "(0x%lx, 0x%lx, 0x%lx)", arg->arg1, arg->arg2, arg->arg3);
   km_warnx("Unsupported");
   arg->hc_ret = -ENOTSUP;
   return HC_CONTINUE;
}

static km_hc_ret_t getcpu_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
   km_infox(KM_TRACE_SCHED, "(0x%lx, 0x%lx, 0x%lx)", arg->arg1, arg->arg2, arg->arg3);
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
   km_infox(KM_TRACE_SCHED, "(0x%lx)", arg->arg1);
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
   arg->hc_ret = __syscall_0(hc);
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

   arg->hc_ret = __syscall_1(hc, km_gva_to_kml(arg->arg1));
   if (arg->hc_ret == 0) {
      // Overwrite Kontain specific info. Buffers 65 bytes each, hardcoded in musl, so we are good
      /*
       * Note: 'kontain-release' is checked in language API bindings.
       * When this changes the bindings need to change too.
       */
      size_t buf_remaining = sizeof(name->release) - strlen(name->release) - 1;
      const char* rel_name = (machine.vm_type == VM_TYPE_KVM) ? ".kontain.KVM" : ".kontain.KKM";
      strncat(name->release, rel_name, buf_remaining);
   }

   return HC_CONTINUE;
}

static int do_exec(char* filename, char** argv, char** envp)
{
   char** newenv;
   char** newargv;
   struct stat statbuf;
   int ret;

   if ((ret = stat(filename, &statbuf)) != 0) {
      char cwd[PATH_MAX];
      if (getcwd(cwd, sizeof(cwd)) == NULL) {
         snprintf(cwd, sizeof(cwd), "<Couldn't get cwd, error %d>", errno);
      }
      km_info(KM_TRACE_HC, "can't stat %s, cwd %s", filename, cwd);
      return -errno;
   }

   // Add some km state to the environment.
   if ((newenv = km_exec_build_env(envp)) == NULL) {
      return -ENOMEM;
   }

   // Build argv line with km program and args before the payload's args.
   if ((newargv = km_exec_build_argv(filename, argv, envp)) == NULL) {
      free(newenv);
      return -ENOEXEC;
   }

   // Start km again with the new payload program
   execve(km_get_self_name(), newargv, newenv);
   // If we are here, execve() failed.  So we need to cleanup.
   km_info(KM_TRACE_HC, "execve failed");
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
   int dirfd = arg->arg1;
   int flags = arg->arg5;
   int open_flag = O_RDONLY | ((flags & AT_SYMLINK_NOFOLLOW) != 0 ? O_NOFOLLOW : 0);

   // Validate the arguments.
   char* pathname = NULL;
   if (arg->arg2 != 0 && (pathname = km_gva_to_kma(arg->arg2)) == NULL) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   if ((pathname == NULL || pathname[0] == '\0') && (flags & AT_EMPTY_PATH) == 0) {
      arg->hc_ret = -EINVAL;
      return HC_CONTINUE;
   }
   if (pathname != NULL) {
      int ret = km_fs_at(dirfd, pathname);
      if (ret < 0) {
         arg->hc_ret = ret;
         return HC_CONTINUE;
      }
   }
   char** argv = km_gva_to_kma(arg->arg3);
   char** envp = km_gva_to_kma(arg->arg4);
   if (argv == NULL || envp == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   // Resolve dirfd and pathname to a host fd open
   int exefd;
   if ((flags & AT_EMPTY_PATH) != 0) {
      exefd = dirfd;
   } else {
      if ((exefd = openat(dirfd, pathname, open_flag)) < 0) {
         arg->hc_ret = -errno;
         return HC_CONTINUE;
      }
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
   pid_t input_pid = (pid_t)arg->arg1;   // force arg1 to a signed 32 bit int
   int rv;
   int* wstatusp = km_gva_to_kma(arg->arg2);

   km_infox(KM_TRACE_VCPU,
            "pid %ld, wstatus 0x%lx, options 0x%lx, rusage 0x%lx",
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4);

   rv = wait4(input_pid, wstatusp, (int)arg->arg3, (struct rusage*)km_gva_to_kma(arg->arg4));
   if (rv < 0) {
      arg->hc_ret = -errno;
   } else {
      arg->hc_ret = rv;
   }
   km_infox(KM_TRACE_HC,
            "wait4 returns %ld, wstatus 0x%x",
            arg->hc_ret,
            wstatusp != NULL ? *wstatusp : -1);
   return HC_CONTINUE;
}

/*
 * int waitid(idtype_t idtype, pid_t id, siginfo_t *infop, int options);
 */
static km_hc_ret_t waitid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   siginfo_t* sip = km_gva_to_kma(arg->arg3);
   pid_t pid = arg->arg2;

   if (sip == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }

   km_infox(KM_TRACE_HC, "waiting for pid %d, options 0x%lx", pid, arg->arg4);
   sip->si_pid = 0;
   if (waitid((idtype_t)arg->arg1, pid, sip, (int)arg->arg4) < 0) {
      arg->hc_ret = -errno;
   } else {
      arg->hc_ret = 0;
   }
   km_infox(KM_TRACE_HC, "returns hc_ret %lu, si_pid %d", arg->hc_ret, sip->si_pid);
   return HC_CONTINUE;
}

/*
 * uid_t geteuid(void);
 * uid_t getuid(void);
 * gid_t getgid(void);
 * gid_t getegid(void);
 */
static km_hc_ret_t getXXid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = __syscall_0(hc);
   return HC_CONTINUE;
}

/*
 * int getgroups(int size, gid_t list[]);
 */
static km_hc_ret_t getgroups_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   gid_t* list = km_gva_to_kma(arg->arg2);
   if (list == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = __syscall_2(hc, arg->arg1, (uint64_t)list);
   return HC_CONTINUE;
}

/*
 * pid_t getsid(pid_t pid);
 */
static km_hc_ret_t getsid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = __syscall_1(hc, arg->arg1);
   return HC_CONTINUE;
}

/*
 * pid_t setsid(void);
 */
static km_hc_ret_t setsid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = __syscall_0(hc);
   return HC_CONTINUE;
}

/*
 * pid_t getpgid(pid_t pid);
 */
static km_hc_ret_t getpgid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = __syscall_1(hc, arg->arg1);
   return HC_CONTINUE;
}

/*
 * int setpgid(pid_t pid, pid_t pgid);
 */
static km_hc_ret_t setpgid_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   km_infox(KM_TRACE_HC, "pid %lu, pgid %lu", arg->arg1, arg->arg2);
   arg->hc_ret = __syscall_2(hc, arg->arg1, arg->arg2);
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

static km_hc_ret_t statfs_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   char* pathname = km_gva_to_kma(arg->arg1);
   void* st = km_gva_to_kma(arg->arg2);
   if (pathname == NULL || st == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_statfs(vcpu, pathname, st);
   return HC_CONTINUE;
}

static km_hc_ret_t fstatfs_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   void* st = km_gva_to_kma(arg->arg2);
   if (st == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   arg->hc_ret = km_fs_fstatfs(vcpu, arg->arg1, st);
   return HC_CONTINUE;
}

static km_hc_ret_t mknodat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
   km_infox(KM_TRACE_HC,
            "hc = %d (mknodat), %ld 0x%lx 0x%lx 0x%lx",
            hc,
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4);
   char* pathname = km_gva_to_kma(arg->arg2);
   if (pathname == NULL) {
      arg->hc_ret = -EFAULT;
   } else {
      arg->hc_ret = __syscall_4(hc, arg->arg1, (uint64_t)pathname, arg->arg3, arg->arg4);
   }
   return HC_CONTINUE;
}

static km_hc_ret_t newfstatat_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   // int newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
   km_infox(KM_TRACE_HC,
            "hc = %d (newfstatat), %ld 0x%lx 0x%lx 0x%lx",
            hc,
            arg->arg1,
            arg->arg2,
            arg->arg3,
            arg->arg4);
   char* pathname = km_gva_to_kma(arg->arg2);
   struct stat* statbuf = km_gva_to_kma(arg->arg3);
   if (pathname == NULL || statbuf == NULL) {
      arg->hc_ret = -EFAULT;
   } else {
      arg->hc_ret = __syscall_4(hc, arg->arg1, (uint64_t)pathname, (uint64_t)statbuf, arg->arg4);
   }
   return HC_CONTINUE;
}

static km_hc_ret_t snapshot_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   km_warnx("SNAPSHOT");

   char* label = km_gva_to_kma(arg->arg1);
   if (arg->arg1 != 0 && label == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   char* description = km_gva_to_kma(arg->arg2);
   if (arg->arg2 != 0 && description == NULL) {
      arg->hc_ret = -EFAULT;
      return HC_CONTINUE;
   }
   int live = arg->arg3;

   // Ensure this VCPU's RIP points at the instruction after the HCALL.
   km_vcpu_sync_rip(vcpu);
   ((km_vcpu_t*)vcpu)->regs_valid = 0;   // force register reread after the sync_rip
   km_read_registers(vcpu);

   // Create the snapshot.
   arg->hc_ret = km_snapshot_create(vcpu, label, description, live);
   // negative value means EBUSY or other similar condition.
   // TODO: in case of live (non zero last arg) returning HC_CONTINUE should just work
   if (arg->hc_ret < 0 || live != 0) {
      return HC_CONTINUE;
   }
   return HC_ALLSTOP;
}

static km_hc_ret_t snapshot_getdata_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = km_snapshot_getdata(vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t snapshot_putdata_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = km_snapshot_putdata(vcpu, km_gva_to_kma(arg->arg1), arg->arg2);
   return HC_CONTINUE;
}

static km_hc_ret_t sched_yield_hcall(void* vcpu, int hc, km_hc_args_t* arg)
{
   arg->hc_ret = sched_yield();
   return HC_CONTINUE;
}

km_hc_stats_t* km_hcalls_stats;
const km_hcall_fn_t km_hcalls_table[KM_MAX_HCALL] = {
    [SYS_arch_prctl] = arch_prctl_hcall,

    [SYS_exit] = exit_hcall,
    [SYS_exit_group] = exit_grp_hcall,
    [SYS_read] = prw_hcall,
    [SYS_write] = prw_hcall,
    [SYS_readv] = prwv_hcall,
    [SYS_writev] = prwv_hcall,
    [SYS_pread64] = prw_hcall,
    [SYS_pwrite64] = prw_hcall,
    [SYS_preadv] = prwv_hcall,
    [SYS_pwritev] = prwv_hcall,
    [SYS_accept] = accept_hcall,
    [SYS_connect] = connect_hcall,
    [SYS_socketpair] = socketpair_hcall,
    [SYS_bind] = bind_hcall,
    [SYS_listen] = listen_hcall,
    [SYS_socket] = socket_hcall,
    [SYS_getsockopt] = getsockopt_hcall,
    [SYS_setsockopt] = setsockopt_hcall,
    [SYS_sendmsg] = sendrecvmsg_hcall,
    [SYS_recvmsg] = sendrecvmsg_hcall,
    [SYS_sendfile] = sendfile_hcall,
    [SYS_copy_file_range] = copy_file_range_hcall,
    [SYS_ioctl] = ioctl_hcall,
    [SYS_fcntl] = fcntl_hcall,
    [SYS_stat] = stat_hcall,
    [SYS_lstat] = lstat_hcall,
    [SYS_statx] = statx_hcall,
    [SYS_fstat] = fstat_hcall,
    [SYS_utimensat] = utimensat_hcall,
    [SYS_getdents] = getdents_hcall,
    [SYS_getdents64] = getdirents_hcall,
    [SYS_getcwd] = getcwd_hcall,
    [SYS_close] = close_hcall,
    [SYS_shutdown] = shutdown_hcall,
    [SYS_brk] = brk_hcall,
    [SYS_futex] = futex_hcall,
    [SYS_mmap] = mmap_hcall,
    [SYS_munmap] = munmap_hcall,
    [SYS_mremap] = mremap_hcall,
    [SYS_mprotect] = mprotect_hcall,
    [SYS_time] = time_hcall,
    [SYS_gettimeofday] = gettimeofday_hcall,
    [SYS_clock_gettime] = clock_time_hcall,
    [SYS_clock_getres] = clock_time_hcall,
    [SYS_clock_settime] = clock_time_hcall,
    [SYS_madvise] = madvise_hcall,
    [SYS_msync] = msync_hcall,

    [SYS_umask] = umask_hcall,
    [SYS_readlink] = readlink_hcall,
    [SYS_readlinkat] = readlinkat_hcall,
    [SYS_getrandom] = getrandom_hcall,
    [SYS_open] = open_hcall,
    [SYS_openat] = openat_hcall,
    [SYS_lseek] = lseek_hcall,
    [SYS_rename] = rename_hcall,
    [SYS_link] = symlink_hcall,
    [SYS_symlink] = symlink_hcall,
    [SYS_mkdir] = mkdir_hcall,
    [SYS_rmdir] = rmdir_hcall,
    [SYS_unlink] = unlink_hcall,
    [SYS_unlinkat] = unlinkat_hcall,
    [SYS_mknod] = mknod_hcall,
    [SYS_chown] = chown_hcall,
    [SYS_lchown] = chown_hcall,
    [SYS_fchown] = fchown_hcall,
    [SYS_chmod] = chmod_hcall,
    [SYS_fchmod] = fchmod_hcall,
    [SYS_chdir] = chdir_hcall,
    [SYS_fchdir] = fchdir_hcall,
    [SYS_truncate] = truncate_hcall,
    [SYS_ftruncate] = ftruncate_hcall,
    [SYS_fsync] = fsync_hcall,
    [SYS_fdatasync] = fdatasync_hcall,
    [SYS_select] = select_hcall,
    [SYS_pselect6] = pselect6_hcall,
    [SYS_pause] = pause_hcall,
    [SYS_sendto] = sendto_hcall,
    [SYS_nanosleep] = nanosleep_hcall,
    [SYS_clock_nanosleep] = clock_nanosleep_hcall,
    [SYS_getsockname] = get_sock_peer_name_hcall,
    [SYS_getpeername] = get_sock_peer_name_hcall,
    [SYS_poll] = poll_hcall,
    [SYS_accept4] = accept4_hcall,
    [SYS_recvfrom] = recvfrom_hcall,
    [SYS_epoll_create1] = epoll1_create_hcall,
    [SYS_epoll_create] = epoll1_create_hcall,
    [SYS_epoll_ctl] = epoll_ctl_hcall,
    [SYS_epoll_wait] = epoll_wait_hcall,
    [SYS_epoll_pwait] = epoll_pwait_hcall,
    [SYS_access] = access_hcall,
    [SYS_dup] = dup_hcall,
    [SYS_dup2] = dup2_hcall,
    [SYS_dup3] = dup3_hcall,
    [SYS_pipe] = pipe_hcall,
    [SYS_pipe2] = pipe2_hcall,
    [SYS_eventfd2] = eventfd2_hcall,
    [SYS_prlimit64] = prlimit64_hcall,

    [SYS_rt_sigprocmask] = rt_sigprocmask_hcall,
    [SYS_rt_sigaction] = rt_sigaction_hcall,
    [SYS_rt_sigreturn] = rt_sigreturn_hcall,
    [SYS_rt_sigpending] = rt_sigpending_hcall,
    [SYS_rt_sigtimedwait] = rt_sigtimedwait,
    [SYS_rt_sigsuspend] = rt_sigsuspend_hcall,
    [SYS_sigaltstack] = sigaltstack_hcall,
    [SYS_kill] = kill_hcall,
    [SYS_tkill] = tkill_hcall,
    [SYS_tgkill] = tgkill_hcall,

    [SYS_getpid] = getpid_hcall,
    [SYS_getppid] = getppid_hcall,
    [SYS_gettid] = gettid_hcall,

    [SYS_getrusage] = getrusage_hcall,
    [SYS_geteuid] = getXXid_hcall,
    [SYS_getuid] = getXXid_hcall,
    [SYS_getegid] = getXXid_hcall,
    [SYS_getgid] = getXXid_hcall,

    [SYS_getgroups] = getgroups_hcall,

    [SYS_setsid] = setsid_hcall,
    [SYS_getsid] = getsid_hcall,
    [SYS_setpgid] = setpgid_hcall,
    [SYS_getpgid] = getpgid_hcall,

    [SYS_sched_yield] = sched_yield_hcall,
    [SYS_setpriority] = dummy_hcall,
    [SYS_sched_getaffinity] = sched_getaffinity_hcall,
    [SYS_sched_setaffinity] = sched_setaffinity_hcall,
    [SYS_prctl] = dummy_hcall,

    [SYS_clone] = clone_hcall,
    [__NR_clone3] = clone3_hcall,   // Fedora 31 (glibc-2.30) has no SYS_clone3 defined
    [SYS_set_tid_address] = set_tid_address_hcall,
    [SYS_membarrier] = dummy_hcall,
    [SYS_getcpu] = getcpu_hcall,
    [SYS_sysinfo] = sysinfo_hcall,

    [SYS_execve] = execve_hcall,
    [SYS_execveat] = execveat_hcall,
    [SYS_fork] = fork_hcall,
    [SYS_vfork] = fork_hcall,
    [SYS_wait4] = wait4_hcall,
    [SYS_waitid] = waitid_hcall,
    [SYS_times] = times_hcall,
    [SYS_getpgrp] = getpgrp_hcall,

    [SYS_set_robust_list] = dummy_hcall,
    [SYS_get_robust_list] = dummy_hcall,

    [SYS_mbind] = dummy_hcall,
    [SYS_flock] = flock_hcall,

    [SYS_uname] = uname_hcall,

    [SYS_setitimer] = setitimer_hcall,
    [SYS_getitimer] = getitimer_hcall,
    [SYS_statfs] = statfs_hcall,
    [SYS_fstatfs] = fstatfs_hcall,

    [SYS_mknodat] = mknodat_hcall,
    [SYS_newfstatat] = newfstatat_hcall,

    [HC_guest_interrupt] = guest_interrupt_hcall,
    [HC_unmapself] = unmapself_hcall,
    [HC_snapshot] = snapshot_hcall,
    [HC_snapshot_getdata] = snapshot_getdata_hcall,
    [HC_snapshot_putdata] = snapshot_putdata_hcall,
};

static void km_print_hcall_stats(void)
{
   for (int hc = 0; hc < KM_MAX_HCALL; hc++) {
      if (km_hcalls_stats[hc].count != 0) {
         km_warnx("%24s(%3d) called\t %9ld times, latency usecs %9ld avg %9ld min %9ld max",
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
   if (km_collect_hc_stats == 1) {
      if ((km_hcalls_stats = calloc(KM_MAX_HCALL, sizeof(km_hc_stats_t))) == NULL) {
         km_err(1, "KVM: no memory for hcall stats");
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
