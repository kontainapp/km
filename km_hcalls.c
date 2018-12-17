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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include "km_hcalls.h"
#include "km.h"

/*
 * User space (km) implementation of hypercalls.
 * These functions are called from kvm_vcpu_run() when guest makes hypercall
 * vmexit.
 *
 * km_hcalls_init() registers hypercalls in the table indexed by hcall #
 * TODO: make registration configurable so only payload specific set of hcalls
 * is registered
 */

static inline uint64_t __syscall_1(uint64_t num, uint64_t a1)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1)
                        : "rcx", "r11", "memory");

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

static inline uint64_t __syscall_3(uint64_t num, uint64_t a1, uint64_t a2,
                                   uint64_t a3)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_4(uint64_t num, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_5(uint64_t num, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5)
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

static inline uint64_t __syscall_6(uint64_t num, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5,
                                   uint64_t a6)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;
   register uint64_t r8 __asm__("r8") = a5;
   register uint64_t r9 __asm__("r9") = a6;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10),
                          "r"(r8), "r"(r9)
                        : "rcx", "r11", "memory");

   return res;
}

/*
 * guest code executed exit(status);
 */
static int halt_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   *status = arg->arg1;
   return 1;
}

/*
 * read/write
 */
static int read_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret =
       __syscall_3(SYS_read, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3);
   return 0;
}

static int write_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret =
       __syscall_3(SYS_write, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3);
   return 0;
}

static int accept_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret = __syscall_3(SYS_accept, arg->arg1, km_gva_to_kma(arg->arg2),
                             km_gva_to_kma(arg->arg3));
   return 0;
}

static int bind_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret =
       __syscall_3(SYS_bind, arg->arg1, km_gva_to_kma(arg->arg2), arg->arg3);
   return 0;
}

static int listen_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret = __syscall_2(SYS_listen, arg->arg1, arg->arg2);
   return 0;
}

static int socket_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret = __syscall_3(SYS_socket, arg->arg1, arg->arg2, arg->arg3);
   return 0;
}

static int getsockopt_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret =
       __syscall_5(SYS_getsockopt, arg->arg1, arg->arg2, arg->arg3,
                   km_gva_to_kma(arg->arg4), km_gva_to_kma(arg->arg5));
   return 0;
}

static int setsockopt_hcall(uint64_t ga, int *status)
{
   km_hc_args_t *arg = (typeof(arg))ga;

   arg->hc_ret = __syscall_5(SYS_setsockopt, arg->arg1, arg->arg2, arg->arg3,
                             km_gva_to_kma(arg->arg4), arg->arg5);
   return 0;
}

km_hcall_fn_t km_hcalls_table[KM_HC_COUNT] = {
    halt_hcall,   read_hcall,   write_hcall,      accept_hcall,     bind_hcall,
    listen_hcall, socket_hcall, getsockopt_hcall, setsockopt_hcall,
};
