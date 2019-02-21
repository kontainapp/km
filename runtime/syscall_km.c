#include "sys/syscall.h"
#include "km_hcalls.h"
#include "syscall_arch.h"

int __syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6)
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

extern int main(int argc, char** argv);

_Noreturn void __start_c__(void)
{
   int argc;
   char** argv;
   int rc = main(argc, argv);

   while (1) {
      __syscall1(SYS_exit, rc);
   }
}