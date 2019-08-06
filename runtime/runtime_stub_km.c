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
   return -ENOSYS;
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
__stub_core__(execve);
__stub_core__(execvp);
__stub__(fork);
__stub__(waitpid);
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
__stub_core__(wait);
__stub_core__(waitid);

/*
 * executable stubs, returning simple canned value
 */
__stub__(backtrace);

const char* gnu_get_libc_version(void)
{
   return "km";
}
