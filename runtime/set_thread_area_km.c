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
