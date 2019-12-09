#include <stdlib.h>
#include "km_hcalls.h"
#include "syscall.h"

_Noreturn void __unmapself(void* base, size_t size)
{
   km_hc_args_t args = {0};

   args.arg1 = base;
   args.arg2 = size;
   km_hcall(HC_unmapself, &args);
}
