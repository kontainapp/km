/*
 * TODO: Header
 */

#include <stdint.h>

/*
 * structures/typedefs/defines from Intel® 64 and IA-32 Architectures SDM
 */
/*
 * SDM, Figure 3-8. Segment Descriptor
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
 * SDM, Table 4-14. pml4e
 */
typedef struct x86_pml4e {
   uint64_t p : 1;                // present
   uint64_t r_w : 1;              // read/write
   uint64_t u_s : 1;              // user/supervisor
   uint64_t pwt : 1;              // page write through
   uint64_t pcd : 1;              // page cache disable
   uint64_t accessed : 1;         //
   uint64_t ign_06 : 1;           //
   uint64_t ps : 1;               // page size, reserved, must be 0
   uint64_t ign_11_08 : 4;        //
   uint64_t pdpt : 40;            // pdpt physaddr
   uint64_t ign_62_52 : 11;       // must be 0
   uint64_t xd : 1;               // exec disable
} x86_pml4e_t /* __attribute__((packed)) */;

/*
 * SDM, Table 4-15. pdpte for 1GB pages
 */
typedef struct x86_pdpte_1g {
   uint64_t p : 1;                 // present
   uint64_t r_w : 1;               // readd/write
   uint64_t u_s : 1;               // user/supervisor
   uint64_t pwt : 1;               // page write through
   uint64_t pcd : 1;               // page cache disable
   uint64_t accessed : 1;          //
   uint64_t dirty : 1;             //
   uint64_t ps : 1;                // page size, 1 means 1GB page
   uint64_t glb : 1;               // global translation if CR4.PGE = 1
   uint64_t ign_11_09 : 3;         //
   uint64_t pat : 1;               //
   uint64_t rsrv_29_13 : 17;       // must be 0
   uint64_t page : 22;             // page physaddr
   uint64_t ign_58_52 : 7;         //
   uint64_t pkey : 4;              // protection key if CR4.PKE = 1
   uint64_t xd : 1;                // exec disable
} x86_pdpte_1g_t /* __attribute__((packed)) */;

/*
 * SDM, Table 4-16. pdpte refering to page directory
 */
typedef struct x86_pdpte {
   uint64_t p : 1;                // present
   uint64_t r_w : 1;              // read/write
   uint64_t u_s : 1;              // user/supervisor
   uint64_t pwt : 1;              // page write through
   uint64_t pcd : 1;              // page cache disable
   uint64_t accessed : 1;         //
   uint64_t ign_6 : 1;            //
   uint64_t ps : 1;               // page size, 1 means 1GB page
   uint64_t ign_11_08 : 4;        //
   uint64_t pd : 40;              // page directory physaddr
   uint64_t ign_62_52 : 11;       //
   uint64_t xd : 1;               // exec disable
} x86_pdpte_t /* __attribute__((packed)) */;

/*
 * SDM, Table 4-16. pdpte refering to page directory
 */
typedef struct x86_pde_2m {
   uint64_t p : 1;                   // present
   uint64_t r_w : 1;                 // read/write
   uint64_t u_s : 1;                 // user/supervisor
   uint64_t pwt : 1;                 // page write through
   uint64_t pcd : 1;                 // page cache disable
   uint64_t accessed : 1;            //
   uint64_t dirty : 1;               //
   uint64_t ps : 1;                  // page size, 1 means 2MB page
   uint64_t glb : 1;                 // global translation if CR4.PGE = 1
   uint64_t ignored_11_09 : 3;       //
   uint64_t pat : 1;                 //
   uint64_t rsrv_20_13 : 8;          // must be 0
   uint64_t page : 31;               // page physaddr
   uint64_t ignored_58_52 : 7;       //
   uint64_t pkey : 4;                // protection key if CR4.PKE = 1
   uint64_t xd : 1;                  // exec disable
} x86_pde_2m_t;

/*
 * SDM, 2.5 CONTROL REGISTERS, Figure 2-7 and surrounding text
 */
/*
 * CR0
 */
#define X86_CR0_PE (1ul << 0)        // Protection Enable
#define X86_CR0_MP (1ul << 1)        // Monitor Coprocessor
#define X86_CR0_EM (1ul << 2)        // Emulation
#define X86_CR0_TS (1ul << 3)        // Task Switched
#define X86_CR0_ET (1ul << 4)        // Extension Type
#define X86_CR0_NE (1ul << 5)        // Numeric Error
#define X86_CR0_WP (1ul << 16)       // Write Protect
#define X86_CR0_AM (1ul << 18)       // Alignment Mask
#define X86_CR0_NW (1ul << 29)       // Not Write-through
#define X86_CR0_CD (1ul << 30)       // Cache Disable
#define X86_CR0_PG (1ul << 31)       // Paging

/*
 * CR3
 */
#define X86_CR3_PWT (1ul << 3)       // Page Write Through
#define X86_CR3_PCD (1ul << 4)       // Page Cache Disable

#define X86_CR3_PCID_MASK 0xfff

#define X86_CR3_PCID_NOFLUSH (1ull << 63)       // Preserve old PCID

/*
 * CR4
 */
#define X86_CR4_VME (1ul << 0)          // enable vm86 extensions
#define X86_CR4_PVI (1ul << 1)          // virtual interrupts flag enable
#define X86_CR4_TSD (1ul << 2)          // disable time stamp at ipl 3
#define X86_CR4_DE (1ul << 3)           // enable debugging extensions
#define X86_CR4_PSE (1ul << 4)          // enable page size extensions
#define X86_CR4_PAE (1ul << 5)          // enable physical address extensions
#define X86_CR4_MCE (1ul << 6)          // Machine check enable
#define X86_CR4_PGE (1ul << 7)          // enable global pages
#define X86_CR4_PCE (1ul << 8)          // enable performance counters at ipl 3
#define X86_CR4_OSFXSR (1ul << 9)       // enable fast FPU save and restore
#define X86_CR4_OSXMMEXCPT (1ul << 10)       // enable unmasked SSE exceptions
#define X86_CR4_UMIP (1ul << 11)             // enable UMIP support
#define X86_CR4_VMXE (1ul << 13)             // enable VMX virtualization
#define X86_CR4_SMXE (1ul << 14)             // enable safer mode (TXT)
#define X86_CR4_FSGSBASE (1ul << 16)         // enable RDWRFSGS support
#define X86_CR4_PCIDE (1ul << 17)            // enable PCID support
#define X86_CR4_OSXSAVE (1ul << 18)          // enable xsave and xrestore
#define X86_CR4_SMEP (1ul << 20)             // enable SMEP support
#define X86_CR4_SMAP (1ul << 21)             // enable SMAP support
#define X86_CR4_PKE (1ul << 22)              // enable Protection Keys support

/*
 * SDM. Figure 2-5. EFLAGS bits, same RFLAGS per 2.3.1
 */
#define X86_RFLAGS_CF (1ul << 0)          // Carry Flag
#define X86_RFLAGS_FIXED (1ul << 1)       // Bit 1 - always on
#define X86_RFLAGS_PF (1ul << 2)          // Parity Flag
#define X86_RFLAGS_AF (1ul << 4)          // Auxiliary carry Flag
#define X86_RFLAGS_ZF (1ul << 6)          // Zero Flag
#define X86_RFLAGS_SF (1ul << 7)          // Sign Flag
#define X86_RFLAGS_TF (1ul << 8)          // Trap Flag
#define X86_RFLAGS_IF (1ul << 9)          // Interrupt Flag
#define X86_RFLAGS_DF (1ul << 10)         // Direction Flag
#define X86_RFLAGS_OF (1ul << 11)         // Overflow Flag
#define X86_RFLAGS_IOPL (3ul << 3)        // I/O Privilege Level (2 bits)
#define X86_RFLAGS_NT (1ul << 14)         // Nested Task
#define X86_RFLAGS_RF (1ul << 16)         // Resume Flag
#define X86_RFLAGS_VM (1ul << 17)         // Virtual Mode
#define X86_RFLAGS_AC (1ul << 18)         // Alignment Check/Access Control
#define X86_RFLAGS_VIF (1ul << 19)        // Virtual Interrupt Flag
#define X86_RFLAGS_VIP (1ul << 20)        // Virtual Interrupt Pending
#define X86_RFLAGS_ID (1ul << 21)         // CPUID detection

/*
 * Intel CPU features in EFER
 */
#define X86_EFER_SCE (1ul << 0)        // SYSCALL/SYSRET
#define X86_EFER_LME (1ul << 8)        // Long mode enable (R/W)
#define X86_EFER_LMA (1ul << 10)       // Long mode active (R/O)
#define X86_EFER_NX (1ul << 11)        // No execute enable
