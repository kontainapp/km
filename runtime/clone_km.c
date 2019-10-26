
#include "km_hcalls.h"
#include <stdint.h>
#include <syscall.h>
#include <sys/types.h>

int __clone(int (*fn)(void *), void *child_stack, int flags, void *args, pid_t *ptid, void *newtls, pid_t *ctid)
{
   /*
    * Clone system call signature is system dependent.
    * The system call signature for X86 is:
    * 
    * long clone(unsigned long flags, void *child_stack,
    *            int *ptid, int *ctid, unsigned long newtls);
    * 
    * See 'man 2 clone for details.
    */
   km_hc_args_t args1 = {.arg1 = flags, .arg2 = (uintptr_t) child_stack, 
                         .arg3 = (uintptr_t) ptid, .arg4 = (uintptr_t) newtls};
   km_hcall(SYS_clone, &args1);
   if (args1.hc_ret != 0) {
      return args1.hc_ret;
   }
   /*
    * Child. Call user supplied function.
    */
   km_hc_args_t args2 = {0};
   args2.hc_ret = fn(args);
   km_hcall(SYS_exit, &args2);
   return -1;
}