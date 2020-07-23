#include "km_hcalls.h"

__asm__(".text");
__asm__(".global __cp_begin");
__asm__(".hidden __cp_begin");
__asm__(".global __cp_end");
__asm__(".hidden __cp_end");
__asm__(".global __cp_cancel");
__asm__(".hidden __cp_cancel");
__asm__(".hidden __cancel");

/*
 * Make a hyper call in a way compatible with musl treatment of cancellation points. This is
 * replacement for musl function with the same name, which in musl case is done in assembly. The
 * point here is to have __cp_begin and __cp_end mark the cancellation point critical section, and
 * jump top cancellation implementation (__cancel) when necessary.
 */
long __syscall_cp_asm(int* c, long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
   __asm__("__cp_begin:");
   if (*c == 0) {
      km_hc_args_t arg;
      register uint64_t rsp asm("rsp");

      arg.hc_rsp = rsp;
      arg.arg1 = a1;
      arg.arg2 = a2;
      arg.arg3 = a3;
      arg.arg4 = a4;
      arg.arg5 = a5;
      arg.arg6 = a6;
      __asm__ __volatile__("mov %0,%%gs:0;"
                           "outl %1, %2"
                           :
                           : "r"(&arg),
                             "a"(0),
                             "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                           : "memory");
      __asm__ __volatile__("\n"
                           :
                           :
                           : "rax");
      __asm__("__cp_end:");
      return arg.hc_ret;
   }
   _Pragma("GCC diagnostic ignored \"-Wreturn-type\"");   // shut up gcc about lack of 'return'
   __asm__("__cp_cancel:"
           "jmp __cancel");
}
