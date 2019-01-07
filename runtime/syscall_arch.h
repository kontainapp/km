#include "km_hcalls.h"

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

static __inline long __syscall0(long n)
{
   km_hc_args_t arg;

   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall1(long n, long a1)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   arg.arg3 = a3;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   arg.arg3 = a3;
   arg.arg4 = a4;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   arg.arg3 = a3;
   arg.arg4 = a4;
   arg.arg5 = a5;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
   km_hc_args_t arg;

   arg.arg1 = a1;
   arg.arg2 = a2;
   arg.arg3 = a3;
   arg.arg4 = a4;
   arg.arg5 = a5;
   arg.arg6 = a6;
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)&arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   return arg.hc_ret;
}
