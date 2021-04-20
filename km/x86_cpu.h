/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#ifndef KM_X86_CPU_H_
#define KM_X86_CPU_H_
#include <stdint.h>

/*
 * structures/typedefs/defines from Intel® 64 and IA-32 Architectures Intel SDM, Vol3
 * These structures are defined for M having a value of 52.
 * See the Intel SDM volume 3 to read about M.
 */
/*
 * Intel SDM, Vol3, Figure 3-8. Segment Descriptor
 */
typedef struct x86_seg_d {
   uint64_t limit_lo : 16;
   uint64_t base_lo : 24;
   uint64_t type : 4;
   uint64_t s : 1;
   uint64_t dpl : 2;
   uint64_t p : 1;
   uint64_t limit_hi : 4;
   uint64_t avl : 1;
   uint64_t l : 1;
   uint64_t d_b : 1;
   uint64_t g : 1;
   uint64_t base_hi : 8;
} x86_seg_d_t /* __attribute__((packed)) */;

/*
 * Intel SDM, Vol3, Table 4-14. pml4e
 */
typedef struct x86_pml4e {
   uint64_t p : 1;            // present
   uint64_t r_w : 1;          // read/write
   uint64_t u_s : 1;          // user/supervisor
   uint64_t pwt : 1;          // page write through
   uint64_t pcd : 1;          // page cache disable
   uint64_t accessed : 1;     //
   uint64_t ign_06 : 1;       //
   uint64_t ps : 1;           // page size, reserved, must be 0
   uint64_t ign_11_08 : 4;    //
   uint64_t pdpt : 40;        // pdpt physaddr
   uint64_t ign_62_52 : 11;   // must be 0
   uint64_t xd : 1;           // exec disable
} x86_pml4e_t /* __attribute__((packed)) */;

/*
 * Intel SDM, Vol3, Table 4-15. pdpte for 1GB pages
 */
typedef struct x86_pdpte_1g {
   uint64_t p : 1;             // present
   uint64_t r_w : 1;           // readd/write
   uint64_t u_s : 1;           // user/supervisor
   uint64_t pwt : 1;           // page write through
   uint64_t pcd : 1;           // page cache disable
   uint64_t accessed : 1;      //
   uint64_t dirty : 1;         //
   uint64_t ps : 1;            // page size, 1 means 1GB page
   uint64_t glb : 1;           // global translation if CR4.PGE = 1
   uint64_t ign_11_09 : 3;     //
   uint64_t pat : 1;           //
   uint64_t rsrv_29_13 : 17;   // must be 0
   uint64_t page : 22;         // page physaddr
   uint64_t ign_58_52 : 7;     //
   uint64_t pkey : 4;          // protection key if CR4.PKE = 1
   uint64_t xd : 1;            // exec disable
} x86_pdpte_1g_t /* __attribute__((packed)) */;

/*
 * Intel SDM, Vol3, Table 4-16. pdpte refering to page directory
 */
typedef struct x86_pdpte {
   uint64_t p : 1;            // present
   uint64_t r_w : 1;          // read/write
   uint64_t u_s : 1;          // user/supervisor
   uint64_t pwt : 1;          // page write through
   uint64_t pcd : 1;          // page cache disable
   uint64_t accessed : 1;     //
   uint64_t ign_6 : 1;        //
   uint64_t ps : 1;           // page size, 1 means 1GB page
   uint64_t ign_11_08 : 4;    //
   uint64_t pd : 40;          // page directory physaddr
   uint64_t ign_62_52 : 11;   //
   uint64_t xd : 1;           // exec disable
} x86_pdpte_t /* __attribute__((packed)) */;

/*
 * Intel SDM, Vol3, Table 4-17. pde refering to 2MB page
 */
typedef struct x86_pde_2m {
   uint64_t p : 1;               // present
   uint64_t r_w : 1;             // read/write
   uint64_t u_s : 1;             // user/supervisor
   uint64_t pwt : 1;             // page write through
   uint64_t pcd : 1;             // page cache disable
   uint64_t accessed : 1;        //
   uint64_t dirty : 1;           //
   uint64_t ps : 1;              // page size, 1 means 2MB page
   uint64_t glb : 1;             // global translation if CR4.PGE = 1
   uint64_t ignored_11_09 : 3;   //
   uint64_t pat : 1;             //
   uint64_t rsrv_20_13 : 8;      // must be 0
   uint64_t page : 31;           // page physaddr
   uint64_t ignored_58_52 : 7;   //
   uint64_t pkey : 4;            // protection key if CR4.PKE = 1
   uint64_t xd : 1;              // exec disable
} x86_pde_2m_t;

/*
 * Intel SDM, Vol3, Table 4-18. pde refering to a page table referring to 4KB pages
 */
typedef struct x86_pde_4k {
   uint64_t p : 1;                // present
   uint64_t r_w : 1;              // read/write
   uint64_t u_s : 1;              // user/supervisor
   uint64_t pwt : 1;              // page write through
   uint64_t pcd : 1;              // page cache disable
   uint64_t accessed : 1;         //
   uint64_t ign_6 : 1;            //
   uint64_t ps : 1;               // page size, must be 0
   uint64_t ignored_11_08 : 4;    //
   uint64_t pta : 40;             // physical address of a page table for 4k pages
   uint64_t ignored_62_52 : 11;   //
   uint64_t xd : 1;               // exec disable
} x86_pde_4k_t;

/*
 * Intel SDM, Vol3, Table 4-19. pte refering to a 4k page
 */
typedef struct x86_pte_4k {
   uint64_t p : 1;           // present
   uint64_t r_w : 1;         // read/write
   uint64_t u_s : 1;         // user/supervisor
   uint64_t pwt : 1;         // page write through
   uint64_t pcd : 1;         // page cache disable
   uint64_t accessed : 1;    //
   uint64_t dirty : 1;       // the page has been written on
   uint64_t pat : 1;         //
   uint64_t global : 1;      //
   uint64_t ign_11_09 : 3;   //
   uint64_t page : 40;       // page physical address
   uint64_t ign_58_52 : 7;   //
   uint64_t pkey : 4;        // protection key
   uint64_t xd : 1;          // execute disable
} x86_pte_4k_t;

/*
 * Intel SDM, Vol3, 2.5 CONTROL REGISTERS, Figure 2-7 and surrounding text
 */
/*
 * CR0
 */
#define X86_CR0_PE (1ul << 0)    // Protection Enable
#define X86_CR0_MP (1ul << 1)    // Monitor Coprocessor
#define X86_CR0_EM (1ul << 2)    // Emulation
#define X86_CR0_TS (1ul << 3)    // Task Switched
#define X86_CR0_ET (1ul << 4)    // Extension Type
#define X86_CR0_NE (1ul << 5)    // Numeric Error
#define X86_CR0_WP (1ul << 16)   // Write Protect
#define X86_CR0_AM (1ul << 18)   // Alignment Mask
#define X86_CR0_NW (1ul << 29)   // Not Write-through
#define X86_CR0_CD (1ul << 30)   // Cache Disable
#define X86_CR0_PG (1ul << 31)   // Paging

/*
 * CR3
 */
#define X86_CR3_PWT (1ul << 3)   // Page Write Through
#define X86_CR3_PCD (1ul << 4)   // Page Cache Disable

#define X86_CR3_PCID_MASK 0xfff

#define X86_CR3_PCID_NOFLUSH (1ull << 63)   // Preserve old PCID

/*
 * CR4
 */
#define X86_CR4_VME (1ul << 0)           // enable vm86 extensions
#define X86_CR4_PVI (1ul << 1)           // virtual interrupts flag enable
#define X86_CR4_TSD (1ul << 2)           // disable time stamp at ipl 3
#define X86_CR4_DE (1ul << 3)            // enable debugging extensions
#define X86_CR4_PSE (1ul << 4)           // enable page size extensions
#define X86_CR4_PAE (1ul << 5)           // enable physical address extensions
#define X86_CR4_MCE (1ul << 6)           // Machine check enable
#define X86_CR4_PGE (1ul << 7)           // enable global pages
#define X86_CR4_PCE (1ul << 8)           // enable performance counters at ipl 3
#define X86_CR4_OSFXSR (1ul << 9)        // enable fast FPU save and restore
#define X86_CR4_OSXMMEXCPT (1ul << 10)   // enable unmasked SSE exceptions
#define X86_CR4_UMIP (1ul << 11)         // enable UMIP support
#define X86_CR4_VMXE (1ul << 13)         // enable VMX virtualization
#define X86_CR4_SMXE (1ul << 14)         // enable safer mode (TXT)
#define X86_CR4_FSGSBASE (1ul << 16)     // enable RDWRFSGS support
#define X86_CR4_PCIDE (1ul << 17)        // enable PCID support
#define X86_CR4_OSXSAVE (1ul << 18)      // enable xsave and xrestore
#define X86_CR4_SMEP (1ul << 20)         // enable SMEP support
#define X86_CR4_SMAP (1ul << 21)         // enable SMAP support
#define X86_CR4_PKE (1ul << 22)          // enable Protection Keys support

/*
 * Intel SDM, Vol3. Figure 2-5. EFLAGS bits, same RFLAGS per 2.3.1
 */
#define X86_RFLAGS_CF (1ul << 0)      // Carry Flag
#define X86_RFLAGS_FIXED (1ul << 1)   // Bit 1 - always on
#define X86_RFLAGS_PF (1ul << 2)      // Parity Flag
#define X86_RFLAGS_AF (1ul << 4)      // Auxiliary carry Flag
#define X86_RFLAGS_ZF (1ul << 6)      // Zero Flag
#define X86_RFLAGS_SF (1ul << 7)      // Sign Flag
#define X86_RFLAGS_TF (1ul << 8)      // Trap Flag
#define X86_RFLAGS_IF (1ul << 9)      // Interrupt Flag
#define X86_RFLAGS_DF (1ul << 10)     // Direction Flag
#define X86_RFLAGS_OF (1ul << 11)     // Overflow Flag
#define X86_RFLAGS_IOPL (3ul << 3)    // I/O Privilege Level (2 bits)
#define X86_RFLAGS_NT (1ul << 14)     // Nested Task
#define X86_RFLAGS_RF (1ul << 16)     // Resume Flag
#define X86_RFLAGS_VM (1ul << 17)     // Virtual Mode
#define X86_RFLAGS_AC (1ul << 18)     // Alignment Check/Access Control
#define X86_RFLAGS_VIF (1ul << 19)    // Virtual Interrupt Flag
#define X86_RFLAGS_VIP (1ul << 20)    // Virtual Interrupt Pending
#define X86_RFLAGS_ID (1ul << 21)     // CPUID detection

#define X86_XCR0_X87 (1ul << 0)         // x87 FPU/MMU state
#define X86_XCR0_SSE (1ul << 1)         // SSE state
#define X86_XCR0_AVX (1ul << 2)         // AVX state
#define X86_XCR0_BNDREGS (1ul << 3)     // BNDREG state
#define X86_XCR0_BNDCSR (1ul << 4)      // BMDCSR state
#define X86_XCR0_OPMASK (1ul << 5)      // OPMASK state
#define X86_XCR0_ZMM_HI256 (1ul << 6)   // ZMM HI256 FPU/MMU
#define X86_XCR0_HI16_ZMM (1ul << 7)    // HI16 ZMM state
#define X86_XCR0_PKRU (1ul << 9)        // PKRU state

#define X86_XCR0_MASK \
   (X86_XCR0_PKRU | X86_XCR0_HI16_ZMM | X86_XCR0_ZMM_HI256 | \
    X86_XCR0_OPMASK | X86_XCR0_BNDCSR | X86_XCR0_BNDREGS | \
    X86_XCR0_AVX | X86_XCR0_SSE | X86_XCR0_X87)

/*
 * Intel CPU features in EFER
 */
#define X86_EFER_SCE (1ul << 0)    // SYSCALL/SYSRET
#define X86_EFER_LME (1ul << 8)    // Long mode enable (R/W)
#define X86_EFER_LMA (1ul << 10)   // Long mode active (R/O)
#define X86_EFER_NX (1ul << 11)    // No execute enable

/*
 * Protected-Mode Exceptions and Interrupts
 * Intel SDM Table 6-1
 */
#define X86_INTR_DE (0)    // Divide Error
#define X86_INTR_DB (1)    // Debug Exception
#define X86_INTR_NMI (2)   // NMI
#define X86_INTR_BP (3)    // Breakpoint
#define X86_INTR_OF (4)    // Overflow
#define X86_INTR_BR (5)    // BOUND Range Exceeded
#define X86_INTR_UD (6)    // Invalid Opcode (Undefined Opcode)
#define X86_INTR_NM (7)    // Device Not Available (No Math Coprocessor)
#define X86_INTR_DF (8)    // Double Fault (includes error, always 0)
// 9 Intel Reserved. Post 386 processors do not generate.
#define X86_INTR_TS (10)   // Invalid TSS (includes error)
#define X86_INTR_NP (11)   // Segment Not Present (includes error)
#define X86_INTR_SS (12)   // Stack-Segment Fault (includes error)
#define X86_INTR_GP (13)   // General Protection (includes error)
#define X86_INTR_PF (14)   // Page Fault (includes error)
// 15 - Intel Reserved
#define X86_INTR_MF (16)   // x87 FPU Floating Point Error (Math Fault)
#define X86_INTR_AC (17)   // Alignment Check (includes error, always 0)
#define X86_INTR_MC (18)   // Machine Check (model dependent)
#define X86_INTR_XM (19)   // SMID Floating Point Exception
#define X86_INTR_VE (20)   // Virtualization Exception
// 21-31 Intel Reserved
// 32-255 User Defined.

#define X86_IDT_NENTRY (256)

/*
 * 64 Bit Interrupt Descriptor Table Entry.
 * Intel SDM, Vol3, Figure 6-7
 */
typedef struct x86_idt_entry {
   uint64_t fp_low : 16;      // Low bits of interrupt handler address
   uint64_t sel_rpl : 2;      // segment selector RPL field
   uint64_t sel_ti : 1;       // segment selector table (0=GDT, 1=LDT)
   uint64_t sel_index : 13;   // segment selector index
   uint64_t ist_index : 3;    // Interrupt Stack Table Index
   uint64_t reserved1 : 5;
   uint64_t type : 5;       // System and Segment Descriptior Type from Table 3-2
   uint64_t dpl : 2;        // Descriptor Pri:vilege Level
   uint64_t p : 1;          // Present
   uint64_t fp_mid : 16;    // Mid bits of interrupt handler address
   uint64_t fp_high : 32;   // High bits of interrupt handler address.
   uint64_t reserved2 : 32;
} x86_idt_entry_t;

#define X86_IDT_NENTRY (256)
#define X86_IDT_SIZE (sizeof(x86_idt_entry_t) * X86_IDT_NENTRY)

/*
 * System and Segment Descriptor Types
 * Intel SDM, Vol3, Table 3-2
 * (IA-32e mode)
 */
#define X86_DSCT_LDT (0x2)
#define X86_DSCT_TSS_AVAIL (0x9)
#define X86_DSCT_TSS_BUSY (0xb)
#define X86_DSCT_CALL_GATE (0xc)
#define X86_DSCT_INTR_GATE (0xe)
#define X86_DSCT_TRAP_GATE (0xf)

/*
 * 64 Bit Interrupt Stack Frame
 * Intel SDM, Vol3, Figure 6-8
 */
typedef struct x86_interrupt_frame {
   uint64_t rip;
   uint64_t cs;
   uint64_t rflags;
   uint64_t rsp;
   uint64_t ss;
} x86_interrupt_frame_t;

/*
 * Handy MSR Values
 * Intel SDM, Volume 4
 */
#define MSR_IA32_STAR 0xc0000081
#define MSR_IA32_LSTAR 0xc0000082
#define MSR_IA32_FMASK 0xc0000084
#define MSR_IA32_TSC 0x00000010

#endif
