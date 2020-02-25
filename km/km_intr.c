/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Enable and handle traps/exceptions in guest.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "km_coredump.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"
#include "x86_cpu.h"

static char* str_intr[256] = {
    "Divide Error",
    "Debug Exception",
    "NMI",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    0,
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection",
    "Page Fault",
    0,
    "Floating Point Error",
    "Alignment Check",
    "Machine Check",
    "SMID Floating Point Exception",
    "Virtualization Exception",
};

/*
 * See Intel SDM, Vol3, Figure 6-7
 */
static inline void build_idt_entry(x86_idt_entry_t* idt, km_gva_t handler, int index)
{
   idt->fp_low = handler & 0xffff;
   idt->fp_mid = (handler >> 16) & 0xffff;
   idt->fp_high = (handler >> 32) & 0xffffffff;
   idt->sel_index = index;
   idt->p = 1;
   idt->type = X86_DSCT_INTR_GATE;
}

void km_init_guest_idt(km_gva_t default_handler, km_gva_t handler_table_gva)
{
   km_gva_t gdt_base;
   km_gva_t idt_base;
   x86_seg_d_t* gdt;
   x86_idt_entry_t* idte;

   /*
    * Create a GDT. Allocate a page. We only need entry 1 for the IDT.
    */
   if ((gdt_base = km_guest_mmap_simple_monitor(KM_PAGE_SIZE)) < 0) {
      err(1, "Failed to allocate guest IDT memory");
   }
   gdt = (x86_seg_d_t*)km_gva_to_kma_nocheck(gdt_base);
   gdt[1].limit_lo = 0xffff;
   gdt[1].type = 0xa;   // code, conforming
   gdt[1].s = 1;        // system segment
   gdt[1].p = 1;        // present
   gdt[1].l = 1;        // 64 bit code segment
   gdt[1].g = 1;

   machine.gdt = gdt_base;
   machine.gdt_size = KM_PAGE_SIZE;

   /*
    * Create the IDT.
    */
   if ((idt_base = km_guest_mmap_simple_monitor(X86_IDT_SIZE)) < 0) {
      err(1, "Failed to allocate guest IDT memory");
   }
   idte = (x86_idt_entry_t*)km_gva_to_kma_nocheck(idt_base);

   int handler_index = 0;
   uint64_t* handler_table_kma = km_gva_to_kma(handler_table_gva);
   for (int i = 0; i < X86_IDT_NENTRY; i++) {
      if (handler_table_kma != NULL && handler_table_kma[handler_index] != 0) {
         build_idt_entry(&idte[i],
                         handler_table_kma[handler_index] + km_guest.km_interrupt_table_adjust,
                         1);
         handler_index++;
      } else {
         build_idt_entry(&idte[i], default_handler, 1);
      }
   }

   machine.idt = idt_base;
   machine.idt_size = X86_IDT_SIZE;
}

/*
 * This array tells whether a given interrupt number includes
 * an error code on the stack.
 *   0 means no error on stack
 *   1 means error on stack
 *  -1 means will not happen in 64-bit mode.
 */
static uint8_t error_included[32] = {
    0, 0, 0, 0, 0, 0, 0,  0,  1,  -1, 1,  1,  1,  1,  1,  -1,
    0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/*
 * This function is called when a guest vcpu causes a fault like illegal instruction
 * or accessing a nonexistant piece of memory.
 * We get here via HC_guest_interrupt.
 * The fault is converted into a signal number here and then passed to the gdb thread
 * if it is active.
 * Then the signal is delivered to the guest vcpu in case it can handle the signal.
 */
void km_handle_interrupt(km_vcpu_t* vcpu)
{
   km_read_registers(vcpu);
   km_read_sregisters(vcpu);

   uint64_t enumber = vcpu->regs.rbx;
   if (enumber >= sizeof(error_included)) {
      errx(1, "Interrupt out of range - %ld", enumber);
   }
   if (error_included[enumber] == -1) {
      errx(1, "Unexpected Interrupt - %ld", enumber);
   }

   uint64_t* rsp_kma = km_gva_to_kma_nocheck(vcpu->regs.rsp);
   vcpu->regs.rax = *rsp_kma++;
   vcpu->regs.rbx = *rsp_kma++;
   vcpu->regs.rdx = *rsp_kma++;
   uint64_t error_code = 0;
   x86_interrupt_frame_t* iframe = (x86_interrupt_frame_t*)rsp_kma;
   if (error_included[enumber]) {
      error_code = *rsp_kma;
      iframe = (x86_interrupt_frame_t*)(rsp_kma + 1);
   }

   km_infox(KM_TRACE_SIGNALS, "ERROR CODE: 0x%lx", error_code);
   km_infox(KM_TRACE_SIGNALS, "       RIP: 0x%lx", iframe->rip);
   km_infox(KM_TRACE_SIGNALS, "        CS: 0x%lx", iframe->cs);
   km_infox(KM_TRACE_SIGNALS, "    RFLAGS: 0x%lx", iframe->rflags);
   km_infox(KM_TRACE_SIGNALS, "       RSP: 0x%lx", iframe->rsp);
   km_infox(KM_TRACE_SIGNALS, "        SS: 0x%lx", iframe->ss);
   if (km_trace_tag_enabled(KM_TRACE_SIGNALS) != 0) {
      km_dump_vcpu(vcpu);
   }

   // Restore register to what they were before the interrupt.
   vcpu->regs.rip = iframe->rip;
   vcpu->regs.rsp = iframe->rsp;
   vcpu->sregs.cs.base = iframe->cs;
   vcpu->sregs.ss.base = iframe->ss;
   km_write_registers(vcpu);
   km_write_sregisters(vcpu);

   /*
    * map processor exceptions to signals in accordance with Table 3.1 from AMD64 ABI
    * https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
    */
   siginfo_t info = {.si_code = SI_KERNEL};
   switch (enumber) {
      case X86_INTR_DE:   // Divide error: SIGFPE
      case X86_INTR_NM:   // Device not available (math coprocessor)
      case X86_INTR_MF:   // Math Fault
         info.si_signo = SIGFPE;
         break;

      case X86_INTR_DB:   // Debug Exception
      case X86_INTR_BP:   // breakpoint
         info.si_signo = SIGTRAP;
         break;

      case X86_INTR_OF:   // Overflow
      case X86_INTR_SS:   // Stack-Segment fault
         break;
      case X86_INTR_GP:   // General Protection: SIGSEGV
         info.si_signo = SIGSEGV;
         info.si_addr = km_find_faulting_address(vcpu);
         break;

      case X86_INTR_PF:   // Page fault: SIGSEGV
         km_infox(KM_TRACE_SIGNALS, "Page Fault: 0x%llx", vcpu->sregs.cr2);
         info.si_signo = SIGSEGV;
         info.si_addr = (void*)vcpu->sregs.cr2;
         break;

      case X86_INTR_NMI:           // NMI
      case X86_INTR_DF:            // Double Fault
      case X86_INTR_TS:            // Invalid TSS
         info.si_signo = SIGSYS;   // no ABI mapping. KM treats as SIGSYS
         break;

      case X86_INTR_UD:   // Undefined instruction: SIGILL
      {
         unsigned char* ins = km_gva_to_kma(iframe->rip);
         /*
          * Special case if not SYSCALL then SIGILL.
          * If is SYSCALL, try to emulate.
          */
         if (ins == NULL || ins[0] != 0x0f || ins[1] != 0x05) {
            info.si_signo = SIGILL;
            break;
         }

         /*
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
         int hc = vcpu->regs.rax;
         if (hc < 0 || hc >= KM_MAX_HCALL || km_hcalls_table[hc] == NULL) {
            // If we don't support the syscall, Treat it as a SIGILL
            info.si_signo = SIGILL;
            break;
         }
         km_hc_args_t args = {
             .arg1 = vcpu->regs.rdi,
             .arg2 = vcpu->regs.rsi,
             .arg3 = vcpu->regs.rdx,
             .arg4 = vcpu->regs.r10,
             .arg5 = vcpu->regs.r8,
             .arg6 = vcpu->regs.r9,
         };
         km_err_msg(0, "SYSCALL call hc=%d", hc);
         km_hc_ret_t hc_ret = km_hcalls_table[hc](vcpu, hc, &args);
         if (hc_ret != HC_CONTINUE) {
            // TODO: Handle stop conditions
            errx(2, "SYSCALL returned no continue: %d", hc_ret);
         }
         vcpu->regs.rax = args.hc_ret;
         vcpu->regs.rip += 2;   // consume SYSCALL instruction
         km_write_registers(vcpu);

         return;
      }

      case X86_INTR_NP:   // Segment not present
      case X86_INTR_BR:   // BOUND Range
      case X86_INTR_MC:   // Machine Check
      case X86_INTR_XM:   // SIMD Exception
      case X86_INTR_VE:   // Virtualization
      case X86_INTR_AC:   // Alignment Check
      default:
         info.si_signo = SIGILL;
         break;
   }

   km_infox(KM_TRACE_SIGNALS,
            "Guest Fault: type:%ld (%s) --> signal:%d (%s)",
            enumber,
            str_intr[enumber],
            info.si_signo,
            strsignal(info.si_signo));

   km_post_signal(vcpu, &info);
}
