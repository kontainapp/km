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
 * Signal trampoline for KM guests. KM starts signal handling at the guest signal
 * guest signal handler itself with the stack setup for return to __km_sigreturn.
 * Most of this file is about describing the stack so it can be correctly unwound.
 * Since this code is now part of km and mapped into the guest address
 * space the information from the .cfi directives is not available to
 * guest space debuggers.  Let's keep the .cfi directives in case we
 * find a way to use in the future.
 */
.include "km_guest.asmh"

    .section .km_guest_text, "ax", @progbits
    .align 16
__km_sigreturn:
    .type __km_sigreturn, @function
    .global __km_sigreturn
    mov $15, %rax
    syscall
/*
 * Trampoline for x86 exception and interrupt handling. IDT entries point here.
 */
    .section .km_guest_text, "ax", @progbits
    .align 16
__km_handle_interrupt:
    .type __km_handle_interrupt, @function
    .global __km_handle_interrupt
    push %rdx
    push %rbx
    push %rax
    mov $0xdeadbeef, %rbx
    mov %rsp, %gs:0         # KM Setup km_hc_args_t on stack for us to use
    mov $0x81fd, %dx        # HC_guest_interrupt
    out %eax, (%dx)         # Enter KM
    hlt                     # Should never hit here.

/*
 * Convienience macro for exception and interrupt handlers.
 */
.macro intr_hand name, num
    .align 16
handler\name :
    push %rdx
    push %rbx
    push %rax
    mov $\num, %rbx
    mov %rsp, %gs:0         # KM Setup km_hc_args_t on stack for us to use
    mov $0x81fd, %dx        # HC_guest_interrupt
    out %eax, (%dx)         # Enter KM
    hlt                     # Should never hit here.

.endm

/*
 * Interrupt handlers
 */
    .align 16
intr_hand UNEX, 0xff
intr_hand DE, 0
intr_hand OF, 4
intr_hand BR, 5
intr_hand UD, 6
intr_hand NM, 7
intr_hand DF, 8
intr_hand GP, 13
intr_hand PF, 14
intr_hand MF, 16
intr_hand AC, 17
intr_hand MC, 18
intr_hand XM, 19
intr_hand VE, 20
intr_hand CP, 21

/*
 * Table used by KM to build IDT entries.
 */
    .section .km_guest_data, "da", @progbits
    .align 16
    .type __km_interrupt_table, @object
    .global __km_interrupt_table
__km_interrupt_table:
    .quad handlerDE         # 0  #DE
    .quad handlerUNEX       # 1  #DB
    .quad handlerUNEX       # 2  NMI
    .quad handlerUNEX       # 3  #BP
    .quad handlerOF         # 4  #OF
    .quad handlerBR         # 5  #BR
    .quad handlerUD         # 6  #UD
    .quad handlerNM         # 7  #NM
    .quad handlerDF         # 8  #DF
    .quad handlerUNEX       # 9  Coprocessor segment overrun
    .quad handlerUNEX       # 10 #TS
    .quad handlerUNEX       # 11 #NP
    .quad handlerUNEX       # 12 #SS
    .quad handlerGP         # 13 #GP
    .quad handlerPF         # 14 #PF
    .quad handlerUNEX       # 15 Reserved
    .quad handlerMF         # 16 #MF
    .quad handlerAC         # 17 #AC
    .quad handlerMC         # 18 #MC
    .quad handlerXM         # 19 #XM
    .quad handlerVE         # 20 #VE
    .quad handlerCP         # 21 #CP
    .quad handlerUNEX       # Rest unexpected
    .quad 0                 # Terminate list

/*
 * Array of per vcpu km_hc_args_t structures.
 */
    .section .km_guest_data_rw, "dwa", @progbits
    .align 64
    .type km_hcargs, @object
    .global km_hcargs
    // Changes to the size of km_hcargs here should be reflected in km_guest.h
km_hcargs:
    .space KVM_MAX_VCPUS * CACHE_LINE_LENGTH, 0

/*
 * SYSCALL handling. This function converts a syscall into
 * the coresponding KM Hypercall.
 * Linux SYSCALL convention:
 * RAX = syscall number
 * RDI = 1st parameter
 * RSI = 2nd parameter
 * RDX = 3rd parameter
 * R10 = 4th parameter
 * R8  = 5th parameter
 * R9  = 6th parameter
 * On entry: rcx contains the return address and r11 contains the
 * callers eflags register.
 * Return value saved in RAX
 */
    .section .km_guest_text, "ax", @progbits
    .align 16
    .type __km_syscall_handler, @function
    .global __km_syscall_handler
__km_syscall_handler:
    // create a km_hcall_t on the stack.
    push %r9    # arg6
    push %r8    # arg5
    push %r10   # arg4
    push %rdx   # arg3
    push %rsi   # arg2
    push %rdi   # arg1
    push %rax   # hc_ret - don't care about value. %rax is convienent.
    mov %rsp,%gs:0  # set this thread's hcargs pointer

    mov %ax, %dx     # Do the KM HCall
    or $KM_HCALL_PORT_BASE, %dx
    out %eax, (%dx)

    mov hc_arg3(%rsp), %rdx  # restore rdx
    mov hc_ret(%rsp), %rax   # Get return code into RAX
    add $HCARG_SIZE, %rsp    # Restore stack

    andq $0x3C7FD7, %r11    # restore the flag register
    push %r11
    popfq

    /*
     * SYSCALL saved address of the next instruction in %rcx.
     * SYSRET is hardcoded to return to PL=3, so we can't use it.
     * We don't change PL or RFLAGS so we can just jump back.
     */
    jmp *%rcx
