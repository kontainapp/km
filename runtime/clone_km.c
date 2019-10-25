
#include "km_hcalls.h"
#include <stdint.h>
#include <syscall.h>
#include <sys/types.h>

int __clone(int (*fn)(void *), void *child_stack, int flags, void *args, pid_t *ptid, void *newtls, pid_t *ctid)
{
   km_hc_args_t args1 = {.arg1 = (uintptr_t) fn, .arg2 = (uintptr_t) child_stack, .arg3 = flags, .arg4 = (uintptr_t) args,
                         .arg5 = (uintptr_t) ptid, .arg6 = (uintptr_t) newtls, /* arg7? */};
   km_hcall(SYS_clone, &args1);
   if (args1.hc_ret != 0) {
      return args1.hc_ret;
   }
   km_hc_args_t args2 = {0};
   args2.hc_ret = fn(args);
   km_hcall(SYS_exit, &args2);
   return -1;
}