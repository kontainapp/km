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
   // Prefix (0 == no prefix)
   unsigned char prefix;
   // REX Headers
   unsigned char rex_present;
   unsigned char rex_w;   // operation size: 1=64 bit
   unsigned char rex_r;   // ModR/M Reg field extention
   unsigned char rex_x;   // Extension of SIB index
   unsigned char rex_b;   // Extension of ModR/M r/m field, SIB base, or Opcode Reg
   // ModR/M fields
   unsigned char modrm_present;
   unsigned char modrm_mode;
   unsigned char modrm_reg;
   unsigned char modrm_rm;
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
      ins->prefix = ins->curbyte;
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
   ins->modrm_reg = (ins->curbyte >> 3) & 0x07;
   ins->modrm_rm = ins->curbyte & 0x07;
}

static inline void decode_sib(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   ins->sib_present = 1;
   ins->sib_scale = (ins->curbyte >> 6) & 0x03;
   ins->sib_index = (ins->curbyte >> 3) & 0x07;
   ins->sib_base = ins->curbyte & 0x07;
}

static inline int decode_in_list(unsigned char opcode, unsigned char* oplist)
{
   for (int i = 0; oplist[i] != 0; i++) {
      if (opcode == oplist[i]) {
         return 1;
      }
   }
   return 0;
}

static inline void find_modrm_fault(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   if (ins->failed_addr != 0) {
      return;
   }
   if (ins->modrm_mode == 0x03) {
      km_infox(KM_TRACE_DECODE, "Register to register");
      return;
   }
   if (ins->modrm_reg == 0x04 || ins->modrm_rm == 0x04) {
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
      km_infox(KM_TRACE_DECODE, "get rm");
      regp = km_reg_ptr(vcpu, ins->rex_b, ins->modrm_rm);
      ins->failed_addr = *regp + ins->disp;
      return;
   }
   // With SIB
   uint64_t* indexp = km_reg_ptr(vcpu, ins->rex_x, ins->sib_index);
   uint64_t* basep = km_reg_ptr(vcpu, ins->rex_b, ins->sib_base);
   uint64_t scale = 1 << ins->sib_scale;
   km_infox(KM_TRACE_DECODE, "base:0x%lx index:0x%lx scale=%ld disp=%d", *basep, *indexp, scale, ins->disp);
   ins->failed_addr = *basep + (*indexp * scale) + ins->disp;
}

unsigned char x86_3byte_0_38_w[] = {0xc8,   // sha1nexte
                                    0xc9,   // sha1msg1
                                    0xca,   // sha1msg2
                                    0xcb,   // sha256rnds2
                                    0xcc,   // sha256msg1
                                    0xcd,   // sha256msg2
                                    0};
unsigned char x86_3byte_0_3a_w[] = {0xcc,   // sha1mds4
                                    0};
unsigned char x86_3byte_66_38_w[] = {0x01,   // vphaddw
                                     0x02,   // vphaddd
                                     0x03,   // vphaddsw
                                     0x04,   // vpmaddubsw
                                     0x05,   // vphsubw
                                     0x06,   // vphsubd
                                     0x07,   // vphsubsw
                                     0x08,   // vpsignb
                                     0x09,   // vpsignw
                                     0x0a,   // vpsignd
                                     0x0b,   // vpmulhrsw
                                     0x0c,   // vpermilps
                                     0x0d,   // vpermilpd
                                     0x0e,   // vtestps
                                     0x0f,   // vtestpd
                                     0x10,   // pblendvb
                                     0x13,   // vcvtph2ps
                                     0x14,   // pblendvps
                                     0x15,   // pblendvpd
                                     0x16,   // vpermps
                                     0x17,   // vptest
                                     0x28,   // vpmuldq
                                     0x29,   // vpcmpeqq
                                     0x2b,   // vpackusdw
                                     0x36,   // vpermd
                                     0x37,   // vpcmpgtq
                                     0x38,   // vpminsb
                                     0x39,   // vpminsd
                                     0x3a,   // vpminuw
                                     0x3b,   // vpminud
                                     0x3c,   // vpmaxsb
                                     0x3d,   // vpmaxsd
                                     0x3e,   // vpmaxuw
                                     0x3f,   // vpmaxud
                                     0x40,   // vpmulld
                                     0x41,   // vphminposuw
                                     0x45,   // vpsrlvd/q
                                     0x46,   // vpsravd
                                     0x47,   // vpsllvd/q
                                     0x58,   // vbroadcastd
                                     0x59,   // vbroadcastq
                                     0x5a,   // vbroadcasti128
                                     0x78,   // vbroadcastb
                                     0x79,   // vbroadcastw
                                     0x90,   // vgatherdd/q
                                     0x91,   // vgatherd/q
                                     0x92,   // vgatherdps/d
                                     0x93,   // vgatherqps/d
                                     0x96,   // vfmaddsub132ps/d
                                     0x97,   // vfmsubadd132ps/d
                                     0x98,   // vfmadd132ps/d
                                     0x99,   // vfmadd132ss/d
                                     0x9a,   // vfmsub132ps/d
                                     0x9b,   // vfmsub132ss/d
                                     0x9c,   // vfnmadd132ps/d
                                     0x9d,   // vfnmadd132ss/d
                                     0x9e,   // vfnmsub132ps/d
                                     0x9f,   // vfnmsub132ss/d
                                     0xa6,   // vfmaddsub213ps/d
                                     0xa7,   // vfmsubadd213ps/d
                                     0xa8,   // vfmadd213ps/d
                                     0xa9,   // vfmadd213ss/d
                                     0xaa,   // vfmsub213ps/d
                                     0xab,   // vfmsub213ss/d
                                     0xac,   // vfnmadd213ps/d
                                     0xad,   // vfnmadd213ss/d
                                     0xae,   // vfnmsub213ps/d
                                     0xaf,   // vfnmsub213ss/d
                                     0xb6,   // vfmaddsub231ps/d
                                     0xb7,   // vfmsubadd231ps/d
                                     0xb8,   // vfmadd231ps/d
                                     0xb9,   // vfmadd231ss/d
                                     0xba,   // vfmsub231ps/d
                                     0xbb,   // vfmsub231ss/d
                                     0xbc,   // vfnmadd231ps/d
                                     0xbd,   // vfnmadd231ss/d
                                     0xbe,   // vfnmsub231ps/d
                                     0xbf,   // vfnmsub231ss/d
                                     0xdb,   // VAESIMC
                                     0xdc,   // VAESENC
                                     0xdd,   // VAESENCLAST
                                     0xde,   // VAESDEC
                                     0xdf,   // VAESDECLAST
                                     0};
unsigned char x86_3byte_66_3a_w[] = {0x01,   // vpermpd
                                     0x02,   // vpblendd
                                     0x04,   // vpermilps
                                     0x05,   // vpermilpd
                                     0x06,   // vperm2f128
                                     0x08,   // vroundps
                                     0x09,   // vroundpd
                                     0x0a,   // vroundss
                                     0x0b,   // vroundsd
                                     0x0c,   // vblendps
                                     0x0d,   // vblendpd
                                     0x0e,   // vblendw
                                     0x0f,   // vpalignr
                                     0x18,   // vinsertf128
                                     0x19,   // vextractf128
                                     0x1d,   // vcvtps2ph
                                     0x38,   // vinserti128
                                     0x39,   // vextracti128
                                     0x40,   // vdpps
                                     0x41,   // vdppd
                                     0x42,   // vmpsadbw
                                     0x44,   // vpclmulqdq
                                     0x46,   // vperm2i128
                                     0x4a,   // vblendvps
                                     0x4b,   // vblendvpd
                                     0x4c,   // vblendvb
                                     0x60,   // vpcmpestrm
                                     0x61,   // vpcmpestri
                                     0x62,   // vpcmpistrm
                                     0x63,   // vpcmpistri
                                     0xdf,   // VAESKEYGEN
                                     0};

static void decode_3byte_opcode(km_vcpu_t* vcpu, x86_instruction_t* ins, unsigned char prevop)
{
   km_warnx("cannot decode 3 byte instruction 0x0f 0x%x 0x%x", prevop, ins->curbyte);
   unsigned char opcode = ins->curbyte;
   decode_consume_byte(vcpu, ins);

   if (ins->prefix == 0x66 && opcode == 0) {
      /*
       * special case. using opcode '0' as end sentinel for opcode lists.
       * got prefix 66, all opcode == 0 (VPSHUFB and VPERMQ)
       */
      find_modrm_fault(vcpu, ins);
      return;
   }
   // pick instruction table based on prefix
   unsigned char* oplist = NULL;
   // Note: No memory 3 byte memory access ops with prefix 0xf2 or 0xf3
   switch (ins->prefix) {
      case 0:
         oplist = (prevop == 0x38) ? x86_3byte_0_38_w : x86_3byte_0_3a_w;
         break;
      case 0x66:
         oplist = (prevop == 0x38) ? x86_3byte_66_38_w : x86_3byte_66_3a_w;
         break;
   }
   if (oplist == NULL) {
      km_infox(KM_TRACE_DECODE, "unrecognized prefix=0x%x", ins->prefix);
      return;
   }

   if (decode_in_list(opcode, oplist) != 0) {
      find_modrm_fault(vcpu, ins);
   }
}

// SDM Vol2 - Table A-3 - no prefix
unsigned char x86_2byte_0_w[] = {0x14,   // vunpcklps
                                 0x15,   // vunpckhps
                                 0x28,   // vmovaps
                                 0x29,   // vmovaps
                                 0x2c,   // cvttps2pi
                                 0x2d,   // cvtps2pi
                                 0x2e,   // vucomiss
                                 0x2f,   // vcomiss
                                 0x51,   // vsqrtps
                                 0x52,   // vrsqrtps
                                 0x53,   // vrcpps
                                 0x54,   // vandps
                                 0x55,   // vandnps
                                 0x56,   // vorps
                                 0x57,   // vxor
                                 0x58,   // vaddps
                                 0x59,   // vmulps
                                 0x5a,   // vcvtps2pd
                                 0x5b,   // vcvtdq2ps
                                 0x5c,   // vsubps
                                 0x5d,   // vminps
                                 0x5e,   // vdivps
                                 0x5f,   // vmaxps
                                 0xc2,   // vcmpps
                                 0xc6,   // vshufps
                                 0};
// SDM Vol2 - Table A-3 - Prefix 0x66
unsigned char x86_2byte_66_w[] = {0x10,   // vmovupd
                                  0x11,   // vmovupd
                                  0x14,   // vunpcklps
                                  0x28,   // vmovapd
                                  0x29,   // vmovapd
                                  0x2c,   // cvttp2pi
                                  0x2d,   // cvtpd2pi
                                  0x2e,   // vucomisd
                                  0x2f,   // vcomisd
                                  0x15,   // vunpckhps
                                  0x51,   // vsqrtpd
                                  0x54,   // vandpd
                                  0x55,   // vandnpd
                                  0x56,   // vorpd
                                  0x57,   // vornpd
                                  0x58,   // vaddpd
                                  0x59,   // vmulpd
                                  0x5a,   // vcvtpd2ps
                                  0x5b,   // vcvtps2dq
                                  0x5c,   // vsubpd
                                  0x5d,   // vminpd
                                  0x5e,   // vdivpd
                                  0x5f,   // vmaxpd
                                  0x60,   // vpunpcklbw
                                  0x61,   // vpunpcklwd
                                  0x62,   // vpunpckldq
                                  0x63,   // vpunpcksswb
                                  0x64,   // vpcmpgtb
                                  0x65,   // vpcmpgtw
                                  0x66,   // vpcmpgtd
                                  0x67,   // vpackuswb
                                  0x68,   // vpunpckhbw
                                  0x69,   // vpunpckhwd
                                  0x6a,   // vpunpckhdq
                                  0x6b,   // vpacksswd
                                  0x6c,   // vpunpcklqdq
                                  0x6d,   // vpunpckhqdq
                                  0x6e,   // vmovd/q
                                  0x6f,   // vmovdqa
                                  0x70,   // vpshufd
                                  0x74,   // vpcmpeqb
                                  0x75,   // vpcmpeqw
                                  0x77,   // vpcmpeqd
                                  0x7c,   // vhaddpd
                                  0x7d,   // vhsubpd
                                  0x7f,   // vmovdqu
                                  0xc2,   // vcmppd
                                  0xc6,   // vshufpd
                                  0xd0,   // vaddsubpd
                                  0xd1,   // vpsrlw
                                  0xd2,   // vpsrld
                                  0xd3,   // vpsrlq
                                  0xd4,   // vpaddq
                                  0xd5,   // vpmullw
                                  0xd6,   // vmovq
                                  0xd7,   // vpmovmskb
                                  0xd8,   // vpsubusb
                                  0xd9,   // vpsubusw
                                  0xda,   // vpminub
                                  0xdb,   // vpand
                                  0xdc,   // vpaddusb
                                  0xdd,   // vpaddusw
                                  0xde,   // vpmaxub
                                  0xdf,   // vpandn
                                  0xe0,   // vpavgb
                                  0xe1,   // vpsraw
                                  0xe2,   // vpsrad
                                  0xe3,   // vpavgw
                                  0xe4,   // vpmulhuw
                                  0xe5,   // vpmulhw
                                  0xe6,   // vcvttpd2dq
                                  0xe7,   // vmovntdq
                                  0xe8,   // vpsubsb
                                  0xe9,   // vpsubsw
                                  0xea,   // vpminsw
                                  0xeb,   // vpor
                                  0xec,   // vpaddsb
                                  0xed,   // vpaddsw
                                  0xee,   // vpmaxsw
                                  0xef,   // vpxor
                                  0xf1,   // vpsllw
                                  0xf2,   // vpslld
                                  0xf3,   // vpsllq
                                  0xf4,   // vpmuludq
                                  0xf5,   // vpmaddwd
                                  0xf6,   // vpsadbw
                                  0xf8,   // vpsubb
                                  0xf9,   // vpsubw
                                  0xfa,   // vpsubd
                                  0xfb,   // vpsubq
                                  0xfc,   // vpaddb
                                  0xfd,   // vpaddw
                                  0xfe,   // vpaddd
                                  0};
// SDM Vol2 - Table A-3 - Prefix 0xf2
unsigned char x86_2byte_f2_w[] = {0x10,   // vmovsd
                                  0x11,   // vmovsd
                                  0x12,   // vmovddup
                                  0x2c,   // vcvttsd2si
                                  0x2d,   // vcvtsd2si
                                  0x51,   // vsqrtsd
                                  0x58,   // vaddsd
                                  0x59,   // vmulsd
                                  0x5a,   // vcvtsd2ss
                                  0x5c,   // vsubdd
                                  0x5d,   // vminsd
                                  0x5e,   // vdivsd
                                  0x5f,   // vmaxsd
                                  0x70,   // vpshuflw
                                  0x7c,   // vhaddps
                                  0x7d,   // vhsubps
                                  0xd0,   // vaddsubps
                                  0xe6,   // vcvtpd2dq
                                  0};
// SDM Vol2 - Table A-3 - Prefix 0xf3
unsigned char x86_2byte_f3_w[] = {0x10,   // vmovss
                                  0x11,   // vmovss
                                  0x12,   // vmovdlp
                                  0x16,   // vmovshdup
                                  0x2c,   // vcvttss2si
                                  0x2d,   // vcvtss2si
                                  0x51,   // vsqrtss
                                  0x52,   // vrsqrtss
                                  0x53,   // vrcpss
                                  0x58,   // vaddss
                                  0x59,   // vmulss
                                  0x5a,   // vcvtss2sd
                                  0x5b,   // vcvttps2dq
                                  0x5c,   // vsubss
                                  0x5d,   // vminss
                                  0x5e,   // vdivss
                                  0x5f,   // vmaxss
                                  0x6f,   // vmovdqu
                                  0x70,   // vpshufhw
                                  0x7e,   // movq
                                  0x7f,   // vmovdqu
                                  0xe6,   // vcvtdq2pd
                                  0};

static void decode_2byte_opcode(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   unsigned char opcode = ins->curbyte;
   decode_consume_byte(vcpu, ins);
   if (opcode == 0x38 || opcode == 0x3a) {
      decode_3byte_opcode(vcpu, ins, opcode);
      return;
   }

   // pick instruction table based on prefix
   unsigned char* oplist = NULL;
   switch (ins->prefix) {
      case 0:
         oplist = x86_2byte_0_w;
         break;
      case 0x66:
         oplist = x86_2byte_66_w;
         break;
      case 0xf2:
         oplist = x86_2byte_f2_w;
         break;
      case 0xf3:
         oplist = x86_2byte_f3_w;
         break;
   }
   if (oplist == NULL) {
      km_infox(KM_TRACE_DECODE, "unrecognized prefix=0x%x", ins->prefix);
      return;
   }

   if (decode_in_list(opcode, oplist) != 0) {
      find_modrm_fault(vcpu, ins);
   }
}

// These opcodes address memory through RSI. Memory type 'X' in SDM
unsigned char x86_rsi_addressed[] = {0xac,   // LODS/B
                                     0xad,   // LODS/W/D/Q
                                     0};

// These opcodes address memory through RDI. Memory type 'Y' in SDM
unsigned char x86_rdi_addressed[] = {0xaa,   // STOS/B
                                     0xab,   // STOS/W/D/Q
                                     0xae,   // SCAS/B
                                     0xaf,   // SCAS/W/D/Q
                                     0};

// These opcode use both RSI and RDI.
unsigned char x86_rsi_rdi_addressed[] = {0xa4,   // MOVS/B
                                         0xa5,   // MOVS/W/D/Q
                                         0xa6,   // CMPS/B
                                         0xa7,   // CMPS/W/D/Q
                                         0};

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
   if (decode_in_list(opcode, x86_rsi_addressed) != 0) {
      ins->failed_addr = vcpu->regs.rsi;
      return;
   }
   if (decode_in_list(opcode, x86_rdi_addressed) != 0) {
      ins->failed_addr = vcpu->regs.rdi;
      return;
   }
   if (decode_in_list(opcode, x86_rsi_rdi_addressed) != 0) {
      if (km_is_gva_accessable(vcpu->regs.rsi, sizeof(uint64_t), PROT_READ) == 0) {
         ins->failed_addr = vcpu->regs.rsi;
      } else {
         ins->failed_addr = vcpu->regs.rdi;
      }
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
               "opcode: 0x%02x modrm: present:%d mode:%d reg:%d rm:%d",
               opcode,
               ins->modrm_present,
               ins->modrm_mode,
               ins->modrm_reg,
               ins->modrm_rm);
      decode_consume_byte(vcpu, ins);
      find_modrm_fault(vcpu, ins);
   } else if (opcode == 0xa5) {
      // MOVS/MOVSB/MOVSW/MOVSQ - These do moves based on RSI and RDI
      km_infox(KM_TRACE_DECODE, "MOVS: RSI:0x%llx RDI:0x%llx", vcpu->regs.rsi, vcpu->regs.rdi);
      if (km_is_gva_accessable(vcpu->regs.rsi, sizeof(uint64_t), PROT_READ) == 0) {
         ins->failed_addr = vcpu->regs.rsi;
      } else if (km_is_gva_accessable(vcpu->regs.rdi, sizeof(uint64_t), PROT_WRITE) == 0) {
         ins->failed_addr = vcpu->regs.rdi;
      }
   } else if (opcode == 0x0f) {
      decode_2byte_opcode(vcpu, ins);
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