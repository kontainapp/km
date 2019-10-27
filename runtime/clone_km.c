
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <sys/types.h>
#include "km_hcalls.h"

void __km_clone_run_child(int (*fn)(void*), void* args)
{
   /*
    * Child. Call user supplied function.
    */
   km_hc_args_t args2 = {0};
   args2.hc_ret = fn(args);
   km_hcall(SYS_exit, &args2);
}

int __clone(int (*fn)(void*), void* child_stack, int flags, void* args, pid_t* ptid, void* newtls, pid_t* ctid)
{ /*
   * Clone system call signature is system dependent.
   * The system call signature for X86 is:
   *
   * long clone(unsigned long flags, void *child_stack,
   *            int *ptid, int *ctid, unsigned long newtls);
   *
   * See 'man 2 clone for details.
   */

   uintptr_t cargs[2] = {(uintptr_t)fn, (uintptr_t)args};
   km_hc_args_t args1 = {.arg1 = flags,
                         .arg2 = (uintptr_t)child_stack,
                         .arg3 = (uintptr_t)ptid,
                         .arg4 = (uintptr_t)ctid,
                         .arg5 = (uintptr_t)newtls,
                         .arg6 = (uintptr_t)cargs};
   km_hcall(SYS_clone, &args1);
   if (args1.hc_ret != 0) {
      if (ctid != NULL && args1.hc_ret != -1) {
         *ctid = (pid_t)args1.hc_ret;
      }
   }
   return args1.hc_ret;
}