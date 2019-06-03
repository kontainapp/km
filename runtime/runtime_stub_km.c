#include <syscall.h>
#include "km_hcalls.h"

void __dummy_stub(void)
{
}

void __dump_core_stub(void)
{
   km_hcall(SYS_exit, (km_hc_args_t*)-1LL);
}

#define __stub__(_func_) void _func_() __attribute__((alias("__dummy_stub")))
#define __stub_core__(_func_) void _func_() __attribute__((alias("__dump_core_stub")))

#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

__stub_core__(execve);
__stub_core__(fork);
__stub_core__(waitpid);
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
