/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Decode and memory as X86_64 instruction and interpret that instruction
 * in order to determine a GVA that caused a memory access failure. Used
 * by km_vcpu_one_kvm_run() when GPA physical protection cases EFAULT to be
 * returned by ioctl(KVM_RUN).
 *
 * See SDM Volume 2 for instruction formats.
 */

#include "km.h"
#include "km_mem.h"

typedef struct x86_instruction {
   km_gva_t failed_addr;    // Initialized to 0
   km_gva_t curip;          // current instruction byte address
   unsigned char curbyte;   // current instruction byte
   // REX Headers
   unsigned char rex_present;
   unsigned char rex_w;   // operation size: 1=64 bit
   unsigned char rex_r;   // ModR/M Reg field extention
   unsigned char rex_x;   // Extension of SIB index
   unsigned char rex_b;   // Extension of ModR/M r/m field, SIB base, or Opcode Reg
   // ModR/M fields
   unsigned char modrm_present;
   unsigned char modrm_mode;
   unsigned char modrm_reg1;
   unsigned char modrm_reg2;
   // SIB fields
   unsigned char sib_present;
   unsigned char sib_scale;
   unsigned char sib_index;
   unsigned char sib_base;
   // Disp
   int32_t disp;
} x86_instruction_t;

static inline void decode_get_byte(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   unsigned char* pc = km_gva_to_kma(ins->curip);
   if (pc == NULL) {
      km_infox(KM_TRACE_DECODE, "Bad RIP: gva:0x%lx", ins->curip);
      ins->failed_addr = ins->curip;
      return;
   }
   ins->curbyte = *pc;
}

static inline void decode_consume_byte(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   ins->curip++;
   decode_get_byte(vcpu, ins);
}

/*
 * Translates fields from an instruction and ModR/M byte into
 * a pointer to a VCPU's register.
 */
static inline uint64_t* km_reg_ptr(km_vcpu_t* vcpu, int b, int reg)
{
   switch (b) {
      case 0:
         // RAX-RDI
         switch (reg) {
            case 0:
               return (uint64_t*)&vcpu->regs.rax;
            case 1:
               return (uint64_t*)&vcpu->regs.rcx;
            case 2:
               return (uint64_t*)&vcpu->regs.rdx;
            case 3:
               return (uint64_t*)&vcpu->regs.rbx;
            case 4:
               return (uint64_t*)&vcpu->regs.rsp;
            case 5:
               return (uint64_t*)&vcpu->regs.rbp;
            case 6:
               return (uint64_t*)&vcpu->regs.rsi;
            case 7:
               return (uint64_t*)&vcpu->regs.rdi;
            default:
               break;
         }
         break;
      // R8-R15
      case 1:
         switch (reg) {
            case 0:
               return (uint64_t*)&vcpu->regs.r8;
            case 1:
               return (uint64_t*)&vcpu->regs.r9;
            case 2:
               return (uint64_t*)&vcpu->regs.r10;
            case 3:
               return (uint64_t*)&vcpu->regs.r11;
            case 4:
               return (uint64_t*)&vcpu->regs.r12;
            case 5:
               return (uint64_t*)&vcpu->regs.r13;
            case 6:
               return (uint64_t*)&vcpu->regs.r14;
            case 7:
               return (uint64_t*)&vcpu->regs.r15;
            default:
               break;
         }
         break;
      default:
         break;
   }
   km_infox(KM_TRACE_DECODE, "bad b or reg: b=%d reg=%d", b, reg);
   return NULL;
}

static inline void decode_legacy_prefixes(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   // TODO : Legacy Prefixes...
   for (;;) {
      switch (ins->curbyte) {
         case 0x26:   // SEG=ES
            break;
         case 0x2e:   // SEG=CS
            break;
         case 0x36:   // SEG=ES
            break;
         case 0x3e:   // SEG=SS
            break;
         case 0x66:   // Operand Size
            break;
         case 0x67:   // Address Size
            break;
         case 0x80:   // Immediate Grp1
         case 0x81:   // Immediate Grp1
         case 0x82:   // Immediate Grp1
         case 0x83:   // Immediate Grp1
            // TODO: Handle Grp1
            break;
         case 0x8f:   // grp1A (POP)
            break;
         case 0xc0:   // Shift GRP2
         case 0xc1:   // Shift GRP2
            break;
         case 0xc6:   // Shift GRP11 (MOV)
         case 0xc7:   // Shift GRP11 (MOV)
            break;
         case 0xd0:   // Shift Grp2
         case 0xd1:   // Shift Grp2
         case 0xd2:   // Shift Grp2
         case 0xd3:   // Shift Grp2
            break;
         case 0xf0:   // LOCK (prefix)
         case 0xf2:   // LOCK (REPNE)
         case 0xf3:   // LOCK (REP)
            break;
         case 0xf6:   // Unary Grp3
         case 0xf7:   // Unary Grp3
         case 0xfe:   // INC/DEC Grp4
         case 0xff:   // INC/DEC Grp5
            break;
         default:
            return;
      }
      km_infox(KM_TRACE_DECODE, "Consume Prefix 0x%x", ins->curbyte);
      decode_consume_byte(vcpu, ins);
      if (ins->failed_addr != 0) {
         return;
      }
   }
   return;
}

static inline void decode_rex_prefix(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   if ((ins->curbyte & 0xf0) != 0x40) {
      return;
   }
   if ((ins->curbyte & 0x08) != 0) {
      ins->rex_w = 1;
   }
   if ((ins->curbyte & 0x04) != 0) {
      ins->rex_r = 1;
   }
   if ((ins->curbyte & 0x02) != 0) {
      ins->rex_x = 1;
   }
   if ((ins->curbyte & 0x01) != 0) {
      ins->rex_b = 1;
   }
   decode_consume_byte(vcpu, ins);
   km_infox(KM_TRACE_DECODE, "REX: W:%d R:%d X:%d B:%d", ins->rex_w, ins->rex_r, ins->rex_x, ins->rex_b);
   return;
}

static inline void decode_modrm(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   ins->modrm_present = 1;
   ins->modrm_mode = (ins->curbyte >> 6) & 0x03;
   ins->modrm_reg1 = (ins->curbyte >> 3) & 0x07;
   ins->modrm_reg2 = ins->curbyte & 0x07;
}

static inline void decode_sib(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   ins->sib_present = 1;
   ins->sib_scale = (ins->curbyte >> 6) & 0x03;
   ins->sib_index = (ins->curbyte >> 3) & 0x07;
   ins->sib_base = ins->curbyte & 0x07;
}

static inline int km_mem_is_source(km_vcpu_t* vcpu, unsigned char opcode)
{
   // TODO: Only works for Opcodes 0x8X
   return (opcode & 0xfe) == 0x8a;
}

static void decode_opcode(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   if (ins->failed_addr != 0) {
      return;
   }

   unsigned char opcode = ins->curbyte;
   decode_consume_byte(vcpu, ins);
   if (ins->failed_addr != 0) {
      return;
   }
   // TEST, XCHG, MOV: ModR/M
   // TODO: Generalize to all instructions with ModR/M byte.
   if (opcode >= 0x84 && opcode <= 0x8b) {
      if (ins->failed_addr != 0) {
         return;
      }
      decode_modrm(vcpu, ins);
      km_infox(KM_TRACE_DECODE,
               "opcode: 0x%02x modrm: present:%d mode:%d reg1:%d reg2:%d",
               opcode,
               ins->modrm_present,
               ins->modrm_mode,
               ins->modrm_reg1,
               ins->modrm_reg2);
      decode_consume_byte(vcpu, ins);
      if (ins->failed_addr != 0) {
         return;
      }
      if (ins->modrm_mode == 0x03) {
         km_infox(KM_TRACE_DECODE, "Register to register");
         return;
      }
      if (ins->modrm_reg1 == 0x04 || ins->modrm_reg2 == 0x04) {
         decode_sib(vcpu, ins);
         km_infox(KM_TRACE_DECODE,
                  " sib: scale:%d index:%d base:%d",
                  ins->sib_scale,
                  ins->sib_index,
                  ins->sib_base);
         decode_consume_byte(vcpu, ins);
         if (ins->failed_addr != 0) {
            return;
         }
      }
      switch (ins->modrm_mode) {
         case 0:
            // no disp
            break;
         case 1:
            // 8 bit disp
            ins->disp = (char)ins->curbyte;
            km_infox(KM_TRACE_DECODE, "1 byte disp 0x%x", ins->disp);
            decode_consume_byte(vcpu, ins);
            if (ins->failed_addr != 0) {
               return;
            }
            break;
         case 2:
            // 32 bit disp
            {
               unsigned char disp[4];
               for (int i = 0; i < 4; i++) {
                  disp[i] = ins->curbyte;
                  decode_consume_byte(vcpu, ins);
                  if (ins->failed_addr != 0) {
                     return;
                  }
               }
               ins->disp = *((uint32_t*)disp);
               km_infox(KM_TRACE_DECODE, "4 byte disp 0x%x", ins->disp);
            }
            break;
         case 3:
            // Register only - No memory, Nothing more to do.
            return;
      }

      if (ins->sib_present == 0) {
         uint64_t* regp = NULL;
         if (km_mem_is_source(vcpu, opcode) != 0) {
            km_infox(KM_TRACE_DECODE, "get reg1");
            regp = km_reg_ptr(vcpu, ins->rex_r, ins->modrm_reg1);
         } else {
            km_infox(KM_TRACE_DECODE, "get reg2");
            regp = km_reg_ptr(vcpu, ins->rex_b, ins->modrm_reg2);
         }
         ins->failed_addr = *regp + ins->disp;
         return;
      }
      // With SIB
      uint64_t* indexp = km_reg_ptr(vcpu, ins->rex_x, ins->sib_index);
      uint64_t* basep = km_reg_ptr(vcpu, ins->rex_b, ins->sib_base);
      uint64_t scale = 1 << ins->sib_scale;
      km_infox(KM_TRACE_DECODE, "base:0x%lx index:0x%lx scale=%ld disp=%d", *basep, *indexp, scale, ins->disp);
      ins->failed_addr = *basep + (*indexp * scale) + ins->disp;
   } else if (opcode == 0xa5) {
      // MOVS/MOVSB/MOVSW/MOVSQ - These do moves based on RSI and RDI
      km_infox(KM_TRACE_DECODE, "MOVS: RSI:0x%llx RDI:0x%llx", vcpu->regs.rsi, vcpu->regs.rdi);
      if (km_is_gva_accessable(vcpu->regs.rsi, sizeof(uint64_t), PROT_READ) == 0) {
         ins->failed_addr = vcpu->regs.rsi;
      } else if (km_is_gva_accessable(vcpu->regs.rdi, sizeof(uint64_t), PROT_WRITE) == 0) {
         ins->failed_addr = vcpu->regs.rdi;
      }
   } else {
      km_infox(KM_TRACE_DECODE, "Uninterpreted Opcode=0x%x", opcode);
   }

   return;
}

static km_gva_t km_x86_decode_fault(km_vcpu_t* vcpu, km_gva_t rip)
{
   x86_instruction_t ins = {.curip = rip};

   km_infox(KM_TRACE_DECODE, "rip=0x%lx", rip);
   if (km_trace_tag_enabled(KM_TRACE_DECODE)) {
      km_dump_vcpu(vcpu);
   }
   decode_get_byte(vcpu, &ins);
   if (ins.failed_addr != 0) {
      goto out;
   }
   decode_legacy_prefixes(vcpu, &ins);
   if (ins.failed_addr != 0) {
      goto out;
   }
   decode_rex_prefix(vcpu, &ins);
   if (ins.failed_addr != 0) {
      goto out;
   }
   decode_opcode(vcpu, &ins);
out:
   // ins.failed_addr is si_addr
   return ins.failed_addr;
}

void* km_find_faulting_address(km_vcpu_t* vcpu)
{
   km_read_registers(vcpu);
   km_gva_t fault = km_x86_decode_fault(vcpu, vcpu->regs.rip);
   km_infox(KM_TRACE_DECODE, "failing address=0x%lx", fault);
   return (void*)fault;
}