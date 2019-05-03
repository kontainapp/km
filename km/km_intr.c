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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "km.h"
#include "km_mem.h"
#include "x86_cpu.h"

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
   size_t gdt_size = sizeof(x86_seg_d_t) * 2;
   km_gva_t idt_base;
   uint64_t* handlers_kma = (uint64_t*)km_gva_to_kma(handlers_gva);
   x86_seg_d_t* gdt;
   x86_idt_entry_t* idte;

   /*
    * Create a GDT with the NULL entry and a single entry for
    * our interrupt handlers
    */
   if (km_syscall_ok(gdt_base = km_guest_mmap_simple(gdt_size)) < 0) {
      err(1, "Failed to allocate guest IDT memory");
   }
   gdt = (x86_seg_d_t*)km_gva_to_kma(gdt_base);
   gdt[1].limit_lo = 0xffff;
   gdt[1].type = 0xa;   // code, conforming
   gdt[1].s = 1;        // system segment
   gdt[1].p = 1;        // present
   gdt[1].l = 1;        // 64 bit code segment
   gdt[1].g = 1;

   machine.gdt = gdt_base;
   machine.gdt_size = gdt_size;

   /*
    * Create the IDT.
    */
   if (km_syscall_ok(idt_base = km_guest_mmap_simple(X86_IDT_SIZE)) < 0) {
      err(1, "Failed to allocate guest IDT memory");
   }
   idte = (x86_idt_entry_t*)km_gva_to_kma(idt_base);

   for (int i = 0; i < X86_IDT_NENTRY; i++) {
      build_idt_entry(&idte[i], *handlers_kma, 1);
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
   kvm_regs_t regs;
   kvm_regs_t sregs;
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

   // Get registers and get references to interrupt stack frame.
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_REGS, &regs) < 0) {
      errx(1, "KVM_GET_REGS failed %d (%s)\n", errno, strerror(errno));
   }
   uint64_t* rsp_kma = km_gva_to_kma(regs.rsp);
   uint64_t error_code = 0;
   x86_interrupt_frame_t* iframe = (x86_interrupt_frame_t*)rsp_kma;
   if (error_included[enumber]) {
      error_code = *rsp_kma;
      iframe = (x86_interrupt_frame_t*)(rsp_kma + 1);
   }

   if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
      errx(1, "KVM_GET_SREGS failed %d (%s)\n", errno, strerror(errno));
   }

   fprintf(stderr, "ERROR CODE: 0x%lx\n", error_code);
   fprintf(stderr, "       RIP: 0x%lx\n", iframe->rip);
   fprintf(stderr, "        CS: 0x%lx\n", iframe->cs);
   fprintf(stderr, "    RFLAGS: 0x%lx\n", iframe->rflags);
   fprintf(stderr, "       RSP: 0x%lx\n", iframe->rsp);
   fprintf(stderr, "        SS: 0x%lx\n", iframe->ss);

   // TODO: do actions.
   switch (events.exception.nr) {
      case X86_INTR_DE:   // Divide error: SIGFPE
         break;

      case X86_INTR_UD:   // Undefined instrucution: SIGILL
         break;

      case X86_INTR_GP:   // General Protection: SIGSEGV
      case X86_INTR_PF:   // Page fault: SIGSEGV
         break;

      // TODO: What about these?
      case X86_INTR_DB:    // Debug Exception
      case X86_INTR_NMI:   // NMI
      case X86_INTR_BP:    // breakpoint
      case X86_INTR_OF:    // Overflow
      case X86_INTR_BR:    // BOUND Range
      case X86_INTR_NM:    // Device not available (math coprocessor)
      case X86_INTR_MF:    // Math Fault
      case X86_INTR_MC:    // Machine Check
      case X86_INTR_XM:    // SIMD Exception
      case X86_INTR_VE:    // Virtualization
      case X86_INTR_DF:    // Double Fault
      case X86_INTR_TS:    // Invalid TSS
      case X86_INTR_NP:    // Segment not present
      case X86_INTR_SS:    // Stack-Segment fault
      case X86_INTR_AC:    // Alignment Check
         break;
      default:
         break;
   }

   errx(events.exception.nr + 1,
        "GOT INTERRUPT HC!!!! number=%d RSP=0x%llx",
        events.exception.nr,
        regs.rsp);
}