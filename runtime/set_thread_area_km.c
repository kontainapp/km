/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
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

#ifndef ARCH_SET_FS
#define ARCH_SET_FS 0x1002
#endif

uint64_t __set_thread_area(uint64_t addr)
{
   return syscall(SYS_arch_prctl, ARCH_SET_FS, addr);
}
