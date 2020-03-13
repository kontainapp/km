/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Signal trampoline for KM guests. KM starts signal handling at the guest signal 
 * guest signal handler itself with the stack setup for return to __km_sigreturn.
 * Most of this file is about describing the stack so it can be correctly unwound.
 */

    nop
.align 16
__km_sigreturn:
    .type __km_sigreturn, @function
    .global __km_sigreturn
    .cfi_startproc
    /* This is a signal handler trampoline */
    .cfi_signal_frame
    /*
     * Describe the saved registers on the stack to DWARF.
     * For .cfi_offset directives:
     *   First parameter is the DWARF register numbers defined here:
     *     http://refspecs.linux-foundation.org/elf/x86_64-abi-0.95.pdf
     *   Second parameter is DWARF stack offset for the register. The
     *   layout conforms to the contents of kvm_regs_t.
     * This code must be kept in sync with signal code in KM to ensure that
     * GDB can interpret stacks that include a signal from KM. 
     */
    .set HCARG_SIZE, 56     # size of km_hc_args_t at top of stack

    .cfi_offset 0, -8 + HCARG_SIZE       # Saved RAX
    .cfi_offset 3, 0 + HCARG_SIZE        # Saved RBX
    .cfi_offset 2, 8 + HCARG_SIZE        # Saved RCX
    .cfi_offset 1, 16 + HCARG_SIZE       # Saved RDX
    .cfi_offset 4, 24 + HCARG_SIZE       # Saved RSI
    .cfi_offset 5, 32 + HCARG_SIZE       # Saved RDI
    .cfi_offset 7, 40 + HCARG_SIZE       # Saved RSP
    .cfi_offset 6, 48 + HCARG_SIZE       # Saved RBP
    .cfi_offset 8, 56 + HCARG_SIZE       # Saved R8
    .cfi_offset 9, 64 + HCARG_SIZE       # Saved R9
    .cfi_offset 10, 72 + HCARG_SIZE      # Saved R10
    .cfi_offset 11, 80 + HCARG_SIZE      # Saved R11
    .cfi_offset 12, 88 + HCARG_SIZE      # Saved R12
    .cfi_offset 13, 96 + HCARG_SIZE      # Saved R13
    .cfi_offset 14, 104 + HCARG_SIZE     # Saved R14
    .cfi_offset 15, 112 + HCARG_SIZE     # Saved R15
    .cfi_offset 16, 120 + HCARG_SIZE     # Saved RIP

    /* The actual code itself */
    mov %esp, %eax          # KM Setup km_hc_args_t on stack for us to use
    mov $0xffff800f, %edx   # SYS_rt_sigreturn
    out %eax, %dx           # Enter KM
    .cfi_endproc
__km_sigreturn_end:        # We'll need this to define the the DWARF 

/*
 * Convienience macro for exception and interrupt handlers.
 */
.macro intr_hand name, num
    .text
.align 16
handler\name :
    push %rdx
    push %rbx
    push %rax
    mov $\num, %rbx
    mov %rsp, %rax          # KM Setup km_hc_args_t on stack for us to use
    mov $0x81fd, %dx        # HC_guest_interrupt
retry\name :
    outl %eax, (%dx)        # Enter KM
    jmp retry\name          # Should never hit here.
    
.endm

/*
 * Interrupt handlers
 */
    .align 16
__km_handle_interrupt:
    .type __km_handle_interrupt, @function
    .global __km_handle_interrupt
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
    .data
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
 * Return value saved in RAX
 */
    .text
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

    // Do the KM HCall
    mov %ax, %dx
    or $0x8000, %dx
    mov %rsp, %rax
    outl %eax, (%dx)

    // Get return code into RAX
    mov (%rsp), %rax
    // Restore stack
    add $56, %rsp

    /*
     * SYSRET is hardcoded to return to PL=3, so
     * we can't use it. We don't change PL or RFLAGS
     * so we can just jump back.
     */
    jmp *%rcx
