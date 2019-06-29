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
