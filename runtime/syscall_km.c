#include "km_hcalls.h"

unsigned long __syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   arg.arg3 = a3;
   arg.arg4 = a4;
   arg.arg5 = a5;
   arg.arg6 = a6;
   km_hcall(n, &arg);
   return arg.hc_ret;
}

extern __typeof(__syscall) syscall __attribute__((__weak__, __alias__("__syscall")));
