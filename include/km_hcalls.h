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

#include <stdint.h>

/*
 * Definitions of hypercalls guest code (payload) can make into the KontainVM.
 * This file is to be included in both KM code and the guest library code.
 */
static const int KM_HCALL_PORT_BASE = 0x8000;

typedef enum km_hcall {
   KM_HC_BASE = 0,
   KM_HC_HLT = KM_HC_BASE,
   KM_HC_READ,
   KM_HC_WRITE,
   KM_HC_ACCEPT,
   KM_HC_BIND,
   KM_HC_LISTEN,
   KM_HC_SOCKET,
   KM_HC_GETSOCKOPT,
   KM_HC_SETSOCKOPT,
   KM_HC_COUNT
} km_hcall_t;

typedef struct km_hc_args {
   uint64_t hc_ret;
   uint64_t arg1;
   uint64_t arg2;
   uint64_t arg3;
   uint64_t arg4;
   uint64_t arg5;
   uint64_t arg6;
} km_hc_args_t;

extern int errno;

static inline int km_hcall(int n, km_hc_args_t *arg)
{
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   /*
    * Linux returns small negative numbers as errors, with abs value that of
    * errno. Linus said "he will make sure no syscall returns a value in -1 ..
    * -4095 as a valid result", leaving that range for errno values
    */
   if (arg->hc_ret > -0x1000UL) {
      errno = -arg->hc_ret;
      return -1;
   }
   return arg->hc_ret;
}
