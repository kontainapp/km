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

#include <unistd.h>

#include "km_hcalls.h"

int errno;

static const char *msg = "Hello, world\n";

static inline void km_hcall(int n, volatile void *arg)
{
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
}

ssize_t write(int fildes, const void *buf, size_t nbyte)
{
   km_rw_hc_t arg = {
       .fd = 1, .r_w = WRITE, .data = (uint64_t)buf, .length = nbyte};

   km_hcall(KM_HC_RW, &arg);
   errno = arg.hc_errno;
   return arg.hc_ret;
}

void exit(int status)
{
   km_hlt_hc_t arg = {.exit_code = status};

   km_hcall(KM_HC_HLT, &arg);
   while (1)
      ;       // squelch ‘noreturn’ function warning
}

int main()
{
   write(1, msg, 14);
   exit(17);
}