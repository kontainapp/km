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

.text
.global __clone
.hidden __clone
.type   __clone,@function
__clone:
   and $-16, %rsi    # rounddown(stack, 16)
   mov 8(%rsp), %r10 # ctid, 7th arg from stack
   # fn in %rdi, args in %rcx
   # hc_args structure
   sub $8, %rsp # arg6
   push %r9     # newtls
   push %r10    # ctid
   push %r8     # ptid
   push %rsi    # stack
   push %rdx    # flags
   sub $8, %rsp # hc_ret
   # clone hc
   mov $0x8038, %edx
   mov %rsp, %gs:0
   outl %eax, (%dx)
   mov 0(%rsp), %eax # Get return code into %rax
   # mov 8(%rsp), %rdx # Restore RDX, although nobody seems to care
   add $56, %rsp
   test %eax,%eax
   jnz 1f         # parent jumps
   # child - on a new stack now
   mov %rdi, %r9  # fn
   mov %rcx, %rdi # args
   call *%r9
   # fn returns, exit now
   sub $56, %rsp
   mov %rsp, %gs:0      # tell km where the hypercall args are
   mov $0x803c, %edx # exit
   outl %eax, (%dx)
   hlt
1:
   ret
