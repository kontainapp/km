/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 * 
 * __syscall_X() simply picks the values from memory into registers and do the
 * system call on behalf of the guest.
 *
 * Some of the values passed in these arguments are guest addresses. There is no
 * pattern here, each system call has its own signature. We need to translate
 * the guest addresses to km view to make things work, using km_gva_to_kma() when
 * appropriate. There is no machinery, need to manually interpret each system
 * call. We paste signature in comment to make it a bit easier. Look into each
 * XXX_hcall() for examples.
 */
static inline uint64_t __syscall_0(uint64_t num)
{
   uint64_t res;

   __asm__ __volatile__("syscall" : "=a"(res) : "a"(num) : "rcx", "r11");

   return res;
}

static inline uint64_t __syscall_1(uint64_t num, uint64_t a1)
{
   uint64_t res;

   __asm__ __volatile__("syscall" : "=a"(res) : "a"(num), "D"(a1) : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_2(uint64_t num, uint64_t a1, uint64_t a2)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
{
   uint64_t res;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t __syscall_4(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t
__syscall_5(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;
   register uint64_t r8 __asm__("r8") = a5;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                        : "rcx", "r11", "memory");

   return res;
}

static inline uint64_t
__syscall_6(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
   uint64_t res;
   register uint64_t r10 __asm__("r10") = a4;
   register uint64_t r8 __asm__("r8") = a5;
   register uint64_t r9 __asm__("r9") = a6;

   __asm__ __volatile__("syscall"
                        : "=a"(res)
                        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                        : "rcx", "r11", "memory");

   return res;
}
