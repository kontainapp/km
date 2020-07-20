#include "km_hcalls.h"

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

static __inline long __syscall0(long n)
{
   long hc_ret;

   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall1(long n, long a1)
{
   long hc_ret;

   __asm__ __volatile__("mov %0,%%gs:8"
                        :
                        : "r"(a1));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
   long hc_ret;

   __asm__ __volatile__(
                        "mov %0,%%gs:8;"
                        "mov %1,%%gs:16;"
                        :
                        : "r"(a1), "r"(a2));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
   long hc_ret;

   __asm__ __volatile__("mov %0,%%gs:8;"
                        "mov %1,%%gs:16;"
                        "mov %2,%%gs:24;"
                        :
                        : "r"(a1), "r"(a2), "r"(a3));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
   long hc_ret;

   __asm__ __volatile__("mov %0,%%gs:8;"
                        "mov %1,%%gs:16;"
                        "mov %2,%%gs:24;"
                        "mov %3,%%gs:32;"
                        :
                        : "r"(a1), "r"(a2), "r"(a3), "r"(a4));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
   long hc_ret;

   __asm__ __volatile__("mov %0,%%gs:8;"
                        "mov %1,%%gs:16;"
                        "mov %2,%%gs:24;"
                        "mov %3,%%gs:32;"
                        "mov %4,%%gs:40;"
                        :
                        : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
   long hc_ret;

   __asm__ __volatile__("mov %0,%%gs:8;"
                        "mov %1,%%gs:16;"
                        "mov %2,%%gs:24;"
                        "mov %3,%%gs:32;"
                        "mov %4,%%gs:40;"
                        "mov %5,%%gs:48;"
                        :
                        : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6));
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"(0),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
   __asm__ __volatile__("\n"
                        :
                        :
                        : "rax");
   __asm__ __volatile__("mov %%gs:0,%0"
                        : "=r"(hc_ret));
   return hc_ret;
}

/*
 * Now that the vvar and vdso segments are mapped into the payload's virtual address
 * space we define these symbols so that harmless syscalls are turned into calls to
 * functions in vdso.
 */
#define VDSO_USEFUL
#define VDSO_CGT_SYM "__vdso_clock_gettime"
#define VDSO_CGT_VER "LINUX_2.6"
#define VDSO_GETCPU_SYM "__vdso_getcpu"
#define VDSO_GETCPU_VER "LINUX_2.6"

