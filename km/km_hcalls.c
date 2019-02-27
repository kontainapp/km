/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
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
#include "km_hcalls.h"
#include "km_mem.h"

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

static inline uint64_t __syscall_1(uint64_t num, uint64_t a1)
{
   uint64_t res;

   __asm__ __volatile__("syscall" : "=a"(res) : "a"(num), "D"(a1) : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_2(uint64_t num, uint64_t a1, uint64_t a2)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_4(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t
__syscall_5(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;
   register uint64_t r8 __asm__("r8") = a5;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t
__syscall_6(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;
   register uint64_t r8 __asm__("r8") = a5;
   register uint64_t r9 __asm__("r9") = a6;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t km_gva_to_kml(uint64_t gva)
{
   return (uint64_t)km_gva_to_kma(gva);
}

/*
 * guest code executed exit(status);
 */
static int exit_hcall(int hc, km_hc_args_t* arg, int* status)
{
   *status = arg->arg1;
   return 1;
}

/*
 * read/write
 */
static int rw_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // ssize_t read(int fd, void *buf, size_t count);
   // ssize_t write(int fd, const void *buf, size_t count);
   arg->hc_ret = __syscall_3(hc, arg->arg1, km_gva_to_kml(arg->arg2), arg->arg3);
   return 0;
}

static int rwv_hcall(int hc, km_hc_args_t* arg, int* status)
{
   int cnt = arg->arg3;
   struct iovec iov[cnt];
   const struct iovec* guest_iov = km_gva_to_kma(arg->arg2);

   // ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
   //
   // need to convert not only the address of iov,
   // but also pointers to individual buffers in it
   for (int i = 0; i < cnt; i++) {
      iov[i].iov_base = km_gva_to_kma((long)guest_iov[i].iov_base);
      iov[i].iov_len = guest_iov[i].iov_len;
   }
   arg->hc_ret = __syscall_3(hc, arg->arg1, (long)iov, cnt);
   return 0;
}

static int accept_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
   arg->hc_ret = __syscall_3(hc, arg->arg1, km_gva_to_kml(arg->arg2), km_gva_to_kml(arg->arg3));
   return 0;
}

static int bind_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
   arg->hc_ret = __syscall_3(hc, arg->arg1, km_gva_to_kml(arg->arg2), arg->arg3);
   return 0;
}

static int listen_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int listen(int sockfd, int backlog);
   arg->hc_ret = __syscall_2(hc, arg->arg1, arg->arg2);
   return 0;
}

static int socket_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int socket(int domain, int type, int protocol);
   arg->hc_ret = __syscall_3(hc, arg->arg1, arg->arg2, arg->arg3);
   return 0;
}

static int getsockopt_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t
   // *optlen);
   arg->hc_ret = __syscall_5(hc,
                             arg->arg1,
                             arg->arg2,
                             arg->arg3,
                             km_gva_to_kml(arg->arg4),
                             km_gva_to_kml(arg->arg5));
   return 0;
}

static int setsockopt_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int setsockopt(int sockfd, int level, int optname, const void *optval,
   // socklen_t optlen);
   arg->hc_ret =
       __syscall_5(hc, arg->arg1, arg->arg2, arg->arg3, km_gva_to_kml(arg->arg4), arg->arg5);
   return 0;
}

static int ioctl_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int ioctl(int fd, unsigned long request, void *arg);
   arg->hc_ret = __syscall_3(hc, arg->arg1, arg->arg2, km_gva_to_kml(arg->arg3));
   return 0;
}

static int stat_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int ioctl(int fd, unsigned long request, void *arg);
   arg->hc_ret = __syscall_2(hc, km_gva_to_kml(arg->arg1), km_gva_to_kml(arg->arg2));
   return 0;
}

static int close_hcall(int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret = __syscall_1(hc, arg->arg1);
   return 0;
}

static int shutdown_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int shutdown(int sockfd, int how);
   arg->hc_ret = __syscall_2(hc, arg->arg1, arg->arg2);
   return 0;
}

static int brk_hcall(int hc, km_hc_args_t* arg, int* status)
{
   arg->hc_ret = km_mem_brk(arg->arg1);
   return 0;
}

static int futex_hcall(int hc, km_hc_args_t* arg, int* status)
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
   return 0;
}

static int mmap_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
   arg->hc_ret = km_guest_mmap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return 0;
};

static int munmap_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int munmap(void *addr, size_t length);
   arg->hc_ret = km_guest_munmap(arg->arg1, arg->arg2);
   return 0;
};

static int mremap_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /* void
   // *new_address */);
   arg->hc_ret = km_guest_mremap(arg->arg1, arg->arg2, arg->arg3, arg->arg4, arg->arg5, arg->arg6);
   return 0;
};

static int clock_gettime_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int clock_gettime(clockid_t clk_id, struct timespec *tp);
   arg->hc_ret = __syscall_2(hc, arg->arg1, km_gva_to_kml(arg->arg2));
   return 0;
}

static int pthread_create_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
   //   void *(*start_routine) (void *), void *arg);
   arg->hc_ret =
       km_create_pthread(km_gva_to_kma(arg->arg1), km_gva_to_kma(arg->arg2), arg->arg3, arg->arg4);
   return 0;
}

static int pthread_join_hcall(int hc, km_hc_args_t* arg, int* status)
{
   // int pthread_join(pthread_t thread, void **retval);
   arg->hc_ret = km_join_pthread(arg->arg1, km_gva_to_kma(arg->arg2));
   return 0;
}

km_hcall_fn_t km_hcalls_table[KM_MAX_HCALL];

void km_hcalls_init(void)
{
   km_hcalls_table[SYS_exit] = exit_hcall;
   km_hcalls_table[SYS_exit_group] = exit_hcall;
   km_hcalls_table[SYS_read] = rw_hcall;
   km_hcalls_table[SYS_write] = rw_hcall;
   km_hcalls_table[SYS_readv] = rwv_hcall;
   km_hcalls_table[SYS_writev] = rwv_hcall;
   km_hcalls_table[SYS_accept] = accept_hcall;
   km_hcalls_table[SYS_bind] = bind_hcall;
   km_hcalls_table[SYS_listen] = listen_hcall;
   km_hcalls_table[SYS_socket] = socket_hcall;
   km_hcalls_table[SYS_getsockopt] = getsockopt_hcall;
   km_hcalls_table[SYS_setsockopt] = setsockopt_hcall;
   km_hcalls_table[SYS_ioctl] = ioctl_hcall;
   km_hcalls_table[SYS_stat] = stat_hcall;
   km_hcalls_table[SYS_close] = close_hcall;
   km_hcalls_table[SYS_shutdown] = shutdown_hcall;
   km_hcalls_table[SYS_brk] = brk_hcall;
   km_hcalls_table[SYS_futex] = futex_hcall;
   km_hcalls_table[SYS_mmap] = mmap_hcall;
   km_hcalls_table[SYS_munmap] = munmap_hcall;
   km_hcalls_table[SYS_mremap] = mremap_hcall;
   km_hcalls_table[SYS_clock_gettime] = clock_gettime_hcall;

   km_hcalls_table[HC_pthread_create] = pthread_create_hcall;
   km_hcalls_table[HC_pthread_join] = pthread_join_hcall;
}

void km_hcalls_fini(void)
{
   /* empty for now */
}
