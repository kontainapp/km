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
 */

#include "km_hcalls.h"
#include "syscall.h"

uint64_t __set_thread_area(uint64_t addr)
{
   km_hc_args_t args;
   args.arg1 = 0x1002;   // ARCH_SET_FS
   args.arg2 = addr;

   km_hcall(SYS_arch_prctl, &args);
   return args.hc_ret;
}
