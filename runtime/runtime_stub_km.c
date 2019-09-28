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

#include <errno.h>
#include <stdio.h>
#include <syscall.h>
#include "km_hcalls.h"

int __dummy_stub(void)
{
   errno = ENOSYS;
   return -1;
}

static void dump_core_stub(const char* syscall_name)
{
   fprintf(stderr, "runtime_km: call to unsupported `%s', generating core dump\n", syscall_name);
   km_hcall(SYS_exit, (km_hc_args_t*)-1LL);
}

#define __stub__(_func_) int _func_() __attribute__((alias("__dummy_stub")))
#define __stub_core__(_func_)                                                                      \
   hidden void __dump_core_stub_##_func_(void) { dump_core_stub(#_func_); }                        \
   void _func_() __attribute__((alias("__dump_core_stub_" #_func_)))

#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

/*
 * Stubs to allow linking but not executing the functionality explicitly removed from libruntime.
 * Guest code still can call syscall directly and trigger KM unimplememnted hypercall.
 */
__stub__(getcontext);
__stub__(setcontext);
__stub__(makecontext);
__stub__(swapcontext);
__stub_core__(execve);
__stub_core__(execvp);
__stub_core__(execv);
__stub_core__(fexecve);
__stub_core__(sched_getparam);
__stub_core__(sched_get_priority_max);
__stub_core__(sched_get_priority_min);
__stub_core__(sched_getscheduler);
__stub_core__(sched_rr_get_interval);
__stub_core__(sched_setparam);
__stub_core__(sched_setscheduler);
__stub_core__(sched_yield);
__stub_core__(system);
__stub__(fork);
__stub__(waitpid);
__stub_core__(wait);
__stub_core__(waitid);

/*
 * executable stubs, returning simple canned value
 */
__stub__(backtrace);
__stub__(mallopt);

const char* gnu_get_libc_version(void)
{
   return "km";
}

// Files compiled with libc sys/sysmacros.h will refer to functions below (#define-d in musl sys/sysmacros.h)
const int gnu_dev_major(int x)
{
   return ((unsigned)((((x) >> 31 >> 1) & 0xfffff000) | (((x) >> 8) & 0x00000fff)));
}

const int gnu_dev_minor(int x)
{
   return ((unsigned)((((x) >> 12) & 0xffffff00) | ((x)&0x000000ff)));
}

const int gnu_dev_makedev(int x, int y)
{
   return ((((x)&0xfffff000ULL) << 32) | (((x)&0x00000fffULL) << 8) | (((y)&0xffffff00ULL) << 12) |
           (((y)&0x000000ffULL)));
}