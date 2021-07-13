/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
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

#ifndef KM_SYSCALL_H_
#define KM_SYSCALL_H_

#include <stdint.h>

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
#endif
