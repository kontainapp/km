/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
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

void km_init_guest_idt(km_gva_t handlers_gva)
{
   km_gva_t gdt_base;
   km_gva_t idt_base;
   x86_seg_d_t* gdt;
   x86_idt_entry_t* idte;

   /*
    * Create a GDT. Allocate a page. We only need entry 1 for the IDT.
    */
   if ((gdt_base = km_guest_mmap_simple(KM_PAGE_SIZE)) < 0) {
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
   if ((idt_base = km_guest_mmap_simple(X86_IDT_SIZE)) < 0) {
      err(1, "Failed to allocate guest IDT memory");
   }
   idte = (x86_idt_entry_t*)km_gva_to_kma_nocheck(idt_base);

   for (int i = 0; i < X86_IDT_NENTRY; i++) {
      build_idt_entry(&idte[i], handlers_gva, 1);
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

void km_handle_interrupt(km_vcpu_t* vcpu)
{
   kvm_vcpu_events_t events;
   uint8_t enumber;

   //  Get event and determine interrupt number
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_VCPU_EVENTS, &events) < 0) {
      errx(1, "KVM_GET_VCPU_EVENTS failed %d (%s)\n", errno, strerror(errno));
   }
   enumber = events.exception.nr;
   if (enumber >= sizeof(error_included)) {
      errx(1, "Interrupt out of range - %d", events.exception.nr);
   }
   if (error_included[enumber] == -1) {
      errx(1, "Unexpected Interrupt - %d", enumber);
   }

   km_read_registers(vcpu);
   km_read_sregisters(vcpu);

   uint64_t* rsp_kma = km_gva_to_kma_nocheck(vcpu->regs.rsp);
   uint64_t error_code = 0;
   x86_interrupt_frame_t* iframe = (x86_interrupt_frame_t*)rsp_kma;
   if (error_included[enumber]) {
      error_code = *rsp_kma;
      iframe = (x86_interrupt_frame_t*)(rsp_kma + 1);
   }

   km_info(KM_TRACE_SIGNALS, "ERROR CODE: 0x%lx\n", error_code);
   km_info(KM_TRACE_SIGNALS, "       RIP: 0x%lx\n", iframe->rip);
   km_info(KM_TRACE_SIGNALS, "        CS: 0x%lx\n", iframe->cs);
   km_info(KM_TRACE_SIGNALS, "    RFLAGS: 0x%lx\n", iframe->rflags);
   km_info(KM_TRACE_SIGNALS, "       RSP: 0x%lx\n", iframe->rsp);
   km_info(KM_TRACE_SIGNALS, "        SS: 0x%lx\n", iframe->ss);

   // Restore register to what they were before the interrupt.
   vcpu->regs.rip = iframe->rip;
   vcpu->regs.rsp = iframe->rsp;
   vcpu->sregs.cs.base = iframe->cs;
   vcpu->sregs.ss.base = iframe->ss;
   km_write_registers(vcpu);
   km_write_sregisters(vcpu);

   /*
    * map processor exceptions to signals in accordance with
    * Table 3.1 from AMD64 ABI
    * https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
    */
   siginfo_t info = {.si_code = SI_KERNEL};
   switch (events.exception.nr) {
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
      case X86_INTR_GP:   // General Protection: SIGSEGV
      case X86_INTR_PF:   // Page fault: SIGSEGV
         info.si_signo = SIGSEGV;
         break;

      case X86_INTR_NMI:           // NMI
      case X86_INTR_DF:            // Double Fault
      case X86_INTR_TS:            // Invalid TSS
         info.si_signo = SIGSYS;   // no ABI mapping. KM treats as SIGSYS
         break;

      case X86_INTR_UD:   // Undefined instruction: SIGILL
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

   warnx("Guest Fault: type:%d (%s) --> signal:%d (%s)",
         events.exception.nr,
         str_intr[events.exception.nr],
         info.si_signo,
         strsignal(info.si_signo));

   if (km_gdb_is_enabled()) {
      vcpu->cpu_run->exit_reason = KVM_EXIT_INTR;
      km_gdb_notify_and_wait(vcpu, info.si_signo);
   }

   km_post_signal(vcpu, &info);

   // We know there is a signal. Force delivery now.
   km_deliver_signal(vcpu);
}