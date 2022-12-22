/*
 * Copyright 2021-2023 Kontain Inc
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
   // opcode
   unsigned char opcode_present;
   unsigned char opcode;
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
 * Also used to xlate the sib.index into a register.
 * The sib.index and the modrm.rm are almost the same but are a
 * little different.  Not sure why these differences don't need
 * to be handled.
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
      // register contents only, not a memory address
      return;
   }
   if (ins->modrm_rm == 0x04) {
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
         ins->disp = 0;
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

   // No SIB.
   if (ins->sib_present == 0) {
      uint64_t* regp = NULL;
      km_infox(KM_TRACE_DECODE, "get rm");
      regp = km_reg_ptr(vcpu, ins->rex_b, ins->modrm_rm);
      ins->failed_addr = *regp + ins->disp;
      return;
      /*
       * If modrm.mod == 0 and modrm.rm == 5 then the address
       * should be [rIP] + disp32 for 64 bit addressing.  Not
       * sure why we don't handle this.
       */
   }

   // With SIB
   uint64_t* basep = km_reg_ptr(vcpu, ins->rex_b, ins->sib_base);
   if (ins->rex_x == 0 && ins->sib_index == 4) {
      ins->failed_addr = *basep + ins->disp;
   } else {
      uint64_t scale = 1 << ins->sib_scale;
      uint64_t* indexp = km_reg_ptr(vcpu, ins->rex_x, ins->sib_index);
      km_infox(KM_TRACE_DECODE, "base:0x%lx index:0x%lx scale=%ld disp=%d", *basep, *indexp, scale, ins->disp);
      ins->failed_addr = *basep + (*indexp * scale) + ins->disp;
   }
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
   unsigned char opcode = ins->curbyte;
   ins->opcode_present = 1;
   ins->opcode = opcode;
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

static void decode_multibyte_opcode(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   unsigned char opcode = ins->curbyte;
   ins->opcode_present = 1;
   ins->opcode = opcode;
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

/*
 * These single byte opcodes use RSI Memory type 'X' in SDM
 */
unsigned char x86_rsi_addressed[] = {0xac,   // LODS/B
                                     0xad,   // LODS/W/D/Q
                                     0};

/*
 * These single byte opcodes use RDI Memory type 'Y' in SDM
 */
unsigned char x86_rdi_addressed[] = {0xaa,   // STOS/B
                                     0xab,   // STOS/W/D/Q
                                     0xae,   // SCAS/B
                                     0xaf,   // SCAS/W/D/Q
                                     0};

/*
 * These single byte opcodes use both RSI and RDI
 */
unsigned char x86_rsi_rdi_addressed[] = {0xa4,   // MOVS/B
                                         0xa5,   // MOVS/W/D/Q
                                         0xa6,   // CMPS/B
                                         0xa7,   // CMPS/W/D/Q
                                         0};

/*
 * Single byte opcodes that use rmmod bytes for register indirect addressing
 * See SDM Table A-2
 */
unsigned char x86_rmmod_addressed[] = {0x01,   // ADD Gv, Ev
                                       0x02,   // ADD Ev, Gb
                                       0x03,   // ADD Ev, Gv
                                       0x08,   // OR  Gb, Eb
                                       0x09,   // OR  Gv, Ev
                                       0x0a,   // OR  Eb, Gb
                                       0x0b,   // OR  Ev, Gv
                                       0x10,   // ADC Gb, Eb
                                       0x11,   // ADC Gv, Ev
                                       0x12,   // ADC Eb, Gb
                                       0x13,   // ADC Ev, Gv
                                       0x18,   // SBB Gb, Eb
                                       0x19,   // SBB Gv, Ev
                                       0x1a,   // SBB Eb, Gb
                                       0x1b,   // SBB Ev, Gv
                                       0x20,   // AND Gb, Eb
                                       0x21,   // AND Gv, Ev
                                       0x22,   // AND Eb, Gb
                                       0x23,   // AND Ev, Gv
                                       0x28,   // SUB Gb, Eb
                                       0x29,   // SUB Gv, Ev
                                       0x2a,   // SUB Eb, Gb
                                       0x2b,   // SUB Ev, Gv
                                       0x30,   // XOR Gb, Eb
                                       0x31,   // XOR Gv, Ev
                                       0x32,   // XOR Eb, Gb
                                       0x33,   // XOR Ev, Gv
                                       0x38,   // CMP Gb, Eb
                                       0x39,   // CMP Gv, Ev
                                       0x3a,   // CMP Eb, Gb
                                       0x3b,   // CMP Ev, Gv
                                       0x63,   // MOVXSD Gv, Ev
                                       0x69,   // IMUL Gv, Ev, lz
                                       0x6b,   // IMUL Gv, Ev, lb
                                       0x84,   // TEST Eb, Gb
                                       0x85,   // TEST Ev, Gv
                                       0x86,   // XCHG Eb, Gb
                                       0x87,   // XCHG Ev, Gv
                                       0x88,   // MOV  Eb, Gb
                                       0x89,   // MOV  Ev, Gv
                                       0x8a,   // MOV  Gb, Eb
                                       0x8b,   // MOV  Gv, Ev
                                       0x8c,   // MOV  Ev, Sw
                                       0x8e,   // MOV  Sw, Ev
                                       0};

static void decode_opcode(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   if (ins->failed_addr != 0) {
      return;
   }

   unsigned char opcode = ins->curbyte;
   ins->opcode_present = 1;
   ins->opcode = opcode;
   decode_consume_byte(vcpu, ins);
   if (ins->failed_addr != 0) {
      return;
   }

   /*
    * multi-byte opcode?
    */
   if (opcode == 0x0f) {
      decode_multibyte_opcode(vcpu, ins);
      return;
   }

   /*
    * One byte opcodes that use mod/rm. 0x00 is ADD. Special case since decode_in_list
    * uses 0 to note end of list.
    */
   if (opcode == 0x00 || decode_in_list(opcode, x86_rmmod_addressed) != 0) {
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
      return;
   }

   /*
    * Single byte opcodes that implicitly use RSI
    */
   if (decode_in_list(opcode, x86_rsi_addressed) != 0) {
      ins->failed_addr = vcpu->regs.rsi;
      return;
   }
   /*
    * Single byte opcodes that implicitly use RDI
    */
   if (decode_in_list(opcode, x86_rdi_addressed) != 0) {
      ins->failed_addr = vcpu->regs.rdi;
      return;
   }
   /*
    * Single byte opcodes that implicitly use both RSI and RDI
    */
   if (decode_in_list(opcode, x86_rsi_rdi_addressed) != 0) {
      if (km_is_gva_accessable(vcpu->regs.rsi, sizeof(uint64_t), PROT_READ) == 0) {
         ins->failed_addr = vcpu->regs.rsi;
      } else {
         ins->failed_addr = vcpu->regs.rdi;
      }
      return;
   }

   uint64_t pc = vcpu->regs.rip;
   km_warnx("KM intruction decode: PC 0x%x, Uninterpreted Instruction=%02x %02x %02x %02x %02x "
            "%02x",
            pc,
            *(unsigned char*)km_gva_to_kma(pc),
            *(unsigned char*)km_gva_to_kma(pc + 1),
            *(unsigned char*)km_gva_to_kma(pc + 2),
            *(unsigned char*)km_gva_to_kma(pc + 3),
            *(unsigned char*)km_gva_to_kma(pc + 4),
            *(unsigned char*)km_gva_to_kma(pc + 5));

   return;
}

/*
 * Code to decode instructions with the vex and xop opcode prefix so we can
 * determine the memory address the instruction was referencing when the fault
 * occurred.
 */
#define VEX_2BYTE_PREFIX 0xc5
#define VEX_3BYTE_PREFIX 0xc4
#define XOP_PREFIX 0x8f
#if 0
// For computing the fault address, we ignore these prefixes.
// This is just here to remind us that the vex pp field can be ignored.
char vex_pp_encoding[] = {
      0,
      0x66, // operand size prefix
      0xf3, // rep, repe, repz prefix
      0xf2  // repne, repnz pefix
};
#endif

/*
 * Constants for both VEX and XOP opcode maps and group maps.
 */
#define OPCODE_MASK 0x00ff
#define OPCODE_GROUP_MASK 0x1f00
#define OPCODE_GROUP_FLAG 0x2000
#define OPCODE_GROUP_0 (OPCODE_GROUP_FLAG | (0 << 8))
#define OPCODE_GROUP_12 (OPCODE_GROUP_FLAG | (12 << 8))
#define OPCODE_GROUP_13 (OPCODE_GROUP_FLAG | (13 << 8))
#define OPCODE_GROUP_14 (OPCODE_GROUP_FLAG | (14 << 8))
#define OPCODE_GROUP_15 (OPCODE_GROUP_FLAG | (15 << 8))
#define OPCODE_GROUP_17 (OPCODE_GROUP_FLAG | (17 << 8))
#define OPCODE_GROUP_31 (OPCODE_GROUP_FLAG | (31 << 8))
#define OPCODE_MAP_END 0xffff

/*
 * The entries in the vex opcode group arrays are the bits --XXX---
 * from the mod r/m byte shifted right 3 and upper 5 bits masked off.
 */
#define VEXGROUP_NUMBER(modrm) (((modrm) >> 3) & 0x07)
#define VEXGROUP_END 0x80

#if 0
// vex groups 12, 13, 14, and 17 do not reference memory.
// Table is here for reference only.
unsigned char vexgroup_12[] = {
   0x02,        // VPSRLW (pp = 1)
   0x04,        // VPSRAW (pp = 1)
   0x06,        // VPSLLW (pp = 1)
   VEXGROUP_END
};
unsigned char vexgroup_13[] = {
   0x02,        // VPSRLD (pp = 1)
   0x04,        // VPSRAD (pp = 1)
   0x06,        // VPSLLD (pp = 1)
   VEXGROUP_END
};
unsigned char vexgroup_14[] = {
   0x02,        // VPSRLQ (pp = 1)
   0x03,        // VPSRLDQ (pp = 1)
   0x06,        // VPSLLQ (pp = 1)
   0x07,        // VPSLLDQ (pp = 1)
   VEXGROUP_END
};
unsigned char vexgroup_17[] = {
   0x01,        // BLSR (pp = 0)
   0x02,        // BLSMSK (pp = 0)
   0x03,        // BLSI (pp = 1)
   VEXGROUP_END
};
#endif
unsigned char vexgroup_15[] = {0x02,   // VLDMXCSR (pp = 0)
                               0x03,   // VSTMXCSR (pp = 1)
                               VEXGROUP_END};
unsigned char* vex_groups[32] = {
#if 0
   [12] = vexgroup_12,
   [13] = vexgroup_13,
   [14] = vexgroup_14,
   [17] = vexgroup_17
#endif
    [15] = vexgroup_15,
};

ushort vex_map1[] = {0x10,
                     0x11,   // VMOVUPS, VMOVUPD, VMOVSS, VMOVSD
                     0x12,
                     0x13,   // VMOVLPS, VMOVLPD, VMOVSLDUP (0x12 only), VMOVDDUP (0x12 only)
                     0x14,   // VUNPCKLPS, VUNPCKLPD
                     0x15,   // VUNPCKHPS, VUNPCKHPD
                     0x16,
                     0x17,   // VMOVHPS, VMOVSHDUP, VMOVHPD
                     0x28,   // VMOVAPS (pp = 0), VMOVAPD (pp = 1)
                     0x29,   // VMOVAPS (pp = 0), VMOVAPD (pp = 1)
                     0x2a,   // VCVTSI2SS (pp = 2), VCVTSI2SD (pp = 3)
                     0x2b,   // VMOVNTPS (pp = 0), VMOVNTPD (pp = 1)
                     0x2c,   // VCVTTSS2SI (pp = 2), VCVTTSD2SI (pp = 3)
                     0x2d,   // VCVTSS2SI (pp = 2), VCVTSD2SI (pp = 3)
                     0x2e,   // VUCOMISS (pp = 0), VUCOMISD (pp = 1)
                     0x2f,   // VCOMISS (pp = 0), VCOMISD (pp = 1)
                     0x51,   // VSQRTPS, VSQRTPD, VSQRTSS, VSQRTSD
                     0x52,   // VRSQRTPS, VRSQRTSS
                     0x53,   // VRCPPS, VRCPSS
                     0x54,   // VANDPS, VANDPD
                     0x55,   // VANDNPS, VANDNPD
                     0x56,   // VORPS, VORPD
                     0x57,   // VXORPS, VXORPD
                     0x58,   // VADDPS (pp = 0), VADDPD (pp = 1), VADDSS (pp = 2), VADDSD (pp = 3)
                     0x59,   // VMULPS (pp = 0), VMULPD (pp = 1), VMULSS (pp = 2), VMULSD (pp = 3)
                     0x5a,   // VCVTPS2PD (pp = 0), VCVTPD2PS (pp = 1), VCVTSS2SD (pp = 2),
                             // VCVTSD2SS (pp = 3)
                     0x5b,   // VCVTDQ2PS (pp = 0), VCVTPS2DQ (pp = 1), VCVTTPS2DQ ( pp = 2)
                     0x5c,   // VSUBPS (pp = 0), VSUBPD (pp = 1), VSUBSS (pp = 2), VSUBSD (pp = 3)
                     0x5d,   // VMINPS (pp = 0), VMINPD (pp = 1), VMINSS (pp = 2), VMINSD (pp = 3)
                     0x5e,   // VDIVPS (pp = 0), VDIVPD (pp = 1), VDIVSS (pp = 2), VDIVSD(pp = 3)
                     0x5f,   // VMAXPS (pp = 0), VMAXPD (pp = 1), VMAXSS (pp = 2), VMAXSD (pp = 3)
                     0x60,   // VPUNPCKLBW
                     0x61,   // VPUNPCKLWD
                     0x62,   // VPUNPCKLDQ
                     0x63,   // VPACKSSWB
                     0x64,   // VPCMPGTB
                     0x65,   // VPCMPGTW
                     0x66,   // VPCMPGTD
                     0x67,   // VPACKUSWB
                     0x68,   // VPUNPCKHBW (pp = 1)
                     0x69,   // VPUNPCKHWD (pp = 1)
                     0x6a,   // VPUNPCKHDQ (pp = 1)
                     0x6b,   // VPACKSSDW (pp = 1)
                     0x6c,   // VPUNPCKLQDQ (pp = 1)
                     0x6d,   // VPUNPCKHQDQ (pp = 1)
                     0x6e,   // VMOVD (pp = 1)
                     0x6f,   // VMOVDQA (pp = 1), VMOVDQU (pp = 2)
                     0x70,   // VPSHUFD, VPSHUFHW, VPSHUFLW
#if 0
   OPCODE_GROUP_12 | 0x71,     // these instructions do not reference memory
   OPCODE_GROUP_13 | 0x72,     // these instructions do not reference memory
   OPCODE_GROUP_14 | 0x73,     // these instructions do not reference memory
#endif
                     0x74,   // VPCMPEQB,
                     0x75,   // VPCMPEQW,
                     0x76,   // VPCMPEQD,
                     0x7c,   // VHADDPD (pp = 1), VHADDPS (pp = 3)
                     0x7d,   // VHSUBPD (pp = 1), VHSUBPS (pp = 3)
                     0x7e,   // VMOVD (pp = 1), VMOVQ (pp = 2)
                     0x7f,   // VMOVDQA (pp = 1), VMOVDQU(pp = 2)
                     OPCODE_GROUP_15 | 0xae,
                     0xc2,   // VCMPccPS, VCMPccPD, VCMPccSS, VCMPccSD
                     0xc4,   // VPINSRW,
                     0xc6,   // VSHUFPS, VSHUFPD
                     0xd0,   // VADDSUBPD (pp = 1), VADDSUBPS (pp = 3)
                     0xd1,   // VPSRLW (pp = 1)
                     0xd2,   // VPSRLD (pp = 1)
                     0xd3,   // VPSRLQ (pp = 1)
                     0xd4,   // VPADDQ (pp = 1)
                     0xd5,   // VPMULLW (pp = 1)
                     0xd8,   // VPSUBUSB (pp = 1)
                     0xd9,   // VPSUBUSW (pp = 1)
                     0xda,   // VPMINUB (pp = 1)
                     0xdb,   // VPAND (pp = 1)
                     0xdc,   // VPADDUSB (pp = 1)
                     0xdd,   // VPADDUSW (pp = 1)
                     0xde,   // VPMAXUB (pp = 1)
                     0xdf,   // VPANDN (pp = 1)
                     0xe0,   // VPAVGB (pp = 1)
                     0xe1,   // VPSRAW (pp = 1)
                     0xe2,   // VPSRAD (pp = 1)
                     0xe3,   // VPAVGW (pp = 1)
                     0xe4,   // VPMULHUW (pp = 1)
                     0xe5,   // VPMULHW (pp = 1)
                     0xe6,   // VCVTTPD2DQ (pp = 1), VCVTDQ2PD (pp = 2), VCVTPD2DQ (pp = 3)
                     0xe8,   // VPSUBSB (pp = 1)
                     0xe9,   // VPSUBSW (pp = 1)
                     0xea,   // VPMINSW (pp = 1)
                     0xeb,   // VPOR (pp = 1)
                     0xec,   // VPADDSB (pp = 1)
                     0xed,   // VPADDSW (pp = 1)
                     0xee,   // VPMAXSW (pp = 1)
                     0xef,   // VPXOR (pp = 1)
                     0xf0,   // VLDDQU (pp = 3)
                     0xf1,   // VPSLLW (pp = 1)
                     0xf2,   // VPSLLD (pp = 1)
                     0xf3,   // VPSLLQ (pp = 1)
                     0xf4,   // VPMULUDQ (pp = 1)
                     0xf5,   // VPMADDWD (pp = 1)
                     0xf6,   // VPSADBW (pp = 1)
                     0xf7,   // VMASKMOVDQU (pp = 1)
                     0xf8,   // VPSUBB (pp = 1)
                     0xf9,   // VPSUBW (pp = 1)
                     0xfa,   // VPSUBD (pp = 1)
                     0xfb,   // VPSUBQ (pp = 1)
                     0xfc,   // VPADDB (pp = 1)
                     0xfd,   // VPADDW (pp = 1)
                     0xfe,   // VPADDD (pp = 1)
                     OPCODE_MAP_END};
ushort vex_map2[] = {0x00,   // VPSHUFB (pp = 1)
                     0x01,   // VPHADDW (pp = 1)
                     0x02,   // VPHADDD (pp = 1)
                     0x03,   // VPHADDSW (pp = 1)
                     0x04,   // VPMADDUBSW (pp = 1)
                     0x05,   // VPHSUBW (pp = 1)
                     0x06,   // VPHSUBD (pp = 1)
                     0x07,   // VPHSUBSW (pp = 1)
                     0x08,   // VPSIGNB (pp = 1)
                     0x09,   // VPSIGNW (pp = 1)
                     0x0a,   // VPSIGND (pp = 1)
                     0x0b,   // VPMULHRSW (pp = 1)
                     0x0c,   // VPERMILPS (pp = 1)
                     0x0d,   // VPERMILPD (pp = 1)
                     0x0e,   // VTESTPS (pp = 1)
                     0x0f,   // VTETSPD (pp = 1)
                     0x13,   // VCVTPH2PS (pp = 1)
                     0x16,   // VPERMPS (pp = 1)
                     0x17,   // VPTEST (pp = 1)
                     0x18,   // VBROADCASTSS (pp = 1)
                     0x19,   // VBROADCASTSD (pp = 1)
                     0x1a,   // VBROADCASTF128 (pp = 1)
                     0x1c,   // VPABSB (pp = 1)
                     0x1d,   // VPABSW (pp = 1)
                     0x1e,   // VPABSD (pp = 1)
                     0x20,   // VPMOVSXBW (pp = 1)
                     0x21,   // VPMOVSXBD (pp = 1)
                     0x22,   // VPMOVSXBQ (pp = 1)
                     0x23,   // VPMOVSXWD (pp = 1)
                     0x24,   // VPMOVSXWQ (pp = 1)
                     0x25,   // VPMOVSXDQ (pp = 1)
                     0x28,   // VPMULDQ (pp = 1)
                     0x29,   // VPCMPEQQ (pp = 1)
                     0x2a,   // VMOVNTDQA (pp = 1)
                     0x2b,   // VPACKUSDW (pp = 1)
                     0x2c,   // VMASKMOVPS (pp = 1) ??
                     0x2d,   // VMASKMOVPD (pp = 1) ??
                     0x2e,   // VMASKMOVPS (pp = 1) ??
                     0x2f,   // VMASKMOVPD (pp = 1) ??
                     0x30,   // VPMOVZXBW (pp = 1)
                     0x31,   // VPMOVZXBD (pp = 1)
                     0x32,   // VPMOVZXBQ (pp = 1)
                     0x33,   // VPMOVZXWD (pp = 1)
                     0x34,   // VPMOVZXWQ (pp = 1)
                     0x35,   // VPMOVZXDQ (pp = 1)
                     0x37,   // VPCMPGTQ (pp = 1)
                     0x38,   // VPMINSB (pp = 1)
                     0x39,   // VPMINSD (pp = 1)
                     0x3a,   // VPMINUW (pp = 1)
                     0x3b,   // VPMINUD (pp = 1)
                     0x3c,   // VPMAXSB (pp = 1)
                     0x3d,   // VPMAXSD (pp = 1)
                     0x3e,   // VPMAXUW (pp = 1)
                     0x3f,   // VPMAXUD (pp = 1)
                     0x40,   // VPMULLD (pp = 1)
                     0x41,   // VPHMINPOSUW (pp = 1)
                     0x58,   // VPBROADCASTD (pp = 1)
                     0x59,   // VPBROADCASTQ (pp = 1)
                     0x5a,   // VPBROADCASTI128 (pp = 1)
                     0x78,   // VPBROADCASTB (pp = 1)
                     0x79,   // VPBROADCASTQ (pp = 1)
                     0x7a,   // VPBROADCASTI128 (pp = 1)
                     0x8c,   // VPMASKMOV (pp = 1)
                     0x8e,   // VPMASKMOV (pp = 1)
                     0x90,   // VPGATHERD (pp = 1)
                     0x91,   // VPGATHERQ (pp = 1)
                     0x92,   // VGATHERD (pp = 1)
                     0x93,   // VGATHERQ (pp = 1)
                     0x96,   // VFMADDSUB132 (pp = 1)  does this one do memory reference?
                     0x97,   // VFMSUBADD132 (pp = 1)
                     0x98,   // VFMADD132 (pp = 1)
                     0x99,   // VFMADD132 (pp = 1)
                     0x9a,   // VFMSUB132 (pp = 1)
                     0x9b,   // VFMSUB132 (pp = 1)
                     0x9c,   // VFNMADD132 (pp = 1)
                     0x9d,   // VFNMADD132 (pp = 1)
                     0x9e,   // VFNMSUB132 (pp = 1)
                     0x9f,   // VFNMSUB132 (pp = 1)
                     0xa6,   // VFMADDSUB213
                     0xa7,   // VFMSUBADD213
                     0xa8,   // VFMADD213 (pp = 1)
                     0xa9,   // VFMADD213 (pp = 1)
                     0xaa,   // VFMSUB213 (pp = 1)
                     0xab,   // VFMSUB213 (pp = 1)
                     0xac,   // VFNMADD213 (pp = 1)
                     0xad,   // VFNMADD213 (pp = 1)
                     0xae,   // VFNMSUB213 (pp = 1)
                     0xaf,   // VFNMSUB213 (pp = 1)
                     0xb6,   // VFMADDSUB231
                     0xb7,   // VFMSUBADD231
                     0xb8,   // VFMADD231 (pp = 1)
                     0xb9,   // VFMADD231 (pp = 1)
                     0xba,   // VFMSUB231 (pp = 1)
                     0xbb,   // VFMSUB231 (pp = 1)
                     0xbc,   // VFNMADD231 (pp = 1)
                     0xbd,   // VFNMADD231 (pp = 1)
                     0xbe,   // VFNMSUB231 (pp = 1)
                     0xbf,   // VFNMSUB231 (pp = 1)
                     0xdb,   // VAESIMC (pp = 1)
                     0xdc,   // VAESENC (pp = 1)
                     0xdd,   // VAESENCLAST (pp = 1)
                     0xde,   // VAESDEC (pp = 1)
                     0xdf,   // VAESDECLAST (pp = 1)
                     0xf2,   // ANDN
#if 0
   OPCODE_GROUP_17 | 0xf3,     // these instructions do not reference memory
#endif
                     0xf5,   // BZHI (pp = 0), PEXT (pp = 1), PDEP (pp = 3)
                     0xf6,   // MULX (pp = 3)
                     0xf7,   // BEXTR (pp = 0), SHLX (pp = 1), SARX (pp = 2), SHRX (pp = 3)
                     OPCODE_MAP_END};
ushort vex_map3[] = {0x00,   // VPERMQ (pp = 1)
                     0x01,   // VPERMPD (pp = 1)
                     0x02,   // VPBLENDD (pp = 1)
                     0x04,   // VPERMILPS (pp = 1)
                     0x05,   // VPERMILPD (pp = 1)
                     0x06,   // VPERM2F128 (pp = 1)
                     0x08,   // VROUNDPS (pp = 1)
                     0x09,   // VROUNDPD (pp = 1)
                     0x0a,   // VROUNDSS (pp = 1)
                     0x0b,   // VROUNDSD (pp = 1)
                     0x0c,   // VBLENDPS (pp = 1)
                     0x0d,   // VBLENDPD (pp = 1)
                     0x0e,   // VPBLENDW (pp = 1)
                     0x0f,   // VPALIGNR (pp = 1)
                     0x14,   // VPEXTRB (pp = 1)
                     0x15,   // VPEXTRW (pp = 1)
                     0x16,   // VPEXTRD (pp = 1)
                     0x17,   // VEXTRACTPS (pp = 1)
                     0x18,   // VINSERTF128 (pp = 1)
                     0x19,   // VEXTRACTF128 (pp = 1)
                     0x1d,   // VCVTPS2PH (pp = 1)
                     0x20,   // VPINSRB (pp = 1)
                     0x21,   // VINSERTPS (pp = 1)
                     0x22,   // VPINSRD, VPINSRQ (pp = 1)
                     0x38,   // VINSERTI128 (pp = 1)
                     0x39,   // VEXTRACTI128 (pp = 1)
                     0x40,   // VDPPS (pp = 1)
                     0x41,   // VDPPD (pp = 1)
                     0x42,   // VMPSADBW (pp = 1)
                     0x44,   // VPCLMULQDQ (pp = 1)
                     0x46,   // VPERM2I128 (pp = 1)
                     0x48,   // VPERMILzz2PS (pp = 1)
                     0x49,   // VPERMILzz2PD (pp = 1)
                     0x4a,   // VBLENDVPS (pp = 1)
                     0x4b,   // VBLENDVPD (pp = 1)
                     0x4c,   // VPBLENDVB (pp = 1)
                     0x5c,   // VFMADDSUBPS (pp = 1)
                     0x5d,   // VFMADDSUBPD (pp = 1)
                     0x5e,   // VFMSUBADDPS (pp = 1)
                     0x5f,   // VFMSUBADDPD (pp = 1)
                     0x60,   // VPCMPESTRM (pp = 1)
                     0x61,   // VPCMPESTRI (pp = 1)
                     0x62,   // VPCMPISTRM (pp = 1)
                     0x63,   // VPCMPISTRI (pp = 1)
                     0x78,   // VFNMADDPS (pp = 1)
                     0x79,   // VFNMADDPD (pp = 1)
                     0x7a,   // VFNMADDSS (pp = 1)
                     0x7b,   // VFNMADDSD (pp = 1)
                     0x7c,   // VFNMSUBPS (pp = 1)
                     0x7d,   // VFNMSUBPD (pp = 1)
                     0x7e,   // VFNMSUBSS (pp = 1)
                     0x7f,   // VFNMSUBSD (pp = 1)
                     0xdf,   // VAESKEYGENASSIST (pp = 1)
                     0xf0,   // RORX (pp = 3)
                     OPCODE_MAP_END};

ushort* vex_maps[32] = {   // 1-3 are valid indexes
    NULL,
    vex_map1,
    vex_map2,
    vex_map3};
// We don't support the xop maps yet.
// But the code is plumbed to use them since they are about the same as
// VEX_3BYTE_PREFIX instruction prefixes.
unsigned char* xop_groups[32];
ushort* xop_maps[32];   // 8-10 are valid indexes

static unsigned short find_opcode_in_map(ushort* map, unsigned char opcode)
{
   ushort* p;

   for (p = map; *p != OPCODE_MAP_END; p++) {
      if ((*p & 0xff) == opcode) {
         break;
      }
   }
   return *p;
}

static void decode_vex_opcodes(km_vcpu_t* vcpu, x86_instruction_t* ins)
{
   int map_select;
   unsigned char prefix = ins->curbyte;
   unsigned short* opcodemap;

   // Consume the first byte of the prefix.
   decode_consume_byte(vcpu, ins);
   if (ins->failed_addr != 0) {
      return;
   }
   switch (prefix) {
      case VEX_3BYTE_PREFIX:
      case XOP_PREFIX:
         // Overload rex_* fields so we can use decode_modrm()
         // Note that field values are complimented for vex and xop.
         ins->rex_r = ((ins->curbyte >> 7) & 1) ^ 1;
         ins->rex_x = ((ins->curbyte >> 6) & 1) ^ 1;
         ins->rex_b = ((ins->curbyte >> 5) & 1) ^ 1;
         map_select = (ins->curbyte & 0x1f);
         opcodemap = (prefix == VEX_3BYTE_PREFIX) ? vex_maps[map_select] : xop_maps[map_select];

         decode_consume_byte(vcpu, ins);
         if (ins->failed_addr != 0) {
            return;
         }
         ins->rex_w = ((ins->curbyte >> 7) & 1) ^ 1;
         // vvvv = (ins->curbyte >> 3) & 0xf;
         // l = (ins->curbyte >> 1) & 1;
         // pp = (ins->curbyte & 3);
         // prefix = vex_pp_encoding[pp];
         break;

      case VEX_2BYTE_PREFIX:
         // 2 byte vex uses default values for many fields.
         ins->rex_x = 0;
         ins->rex_b = 0;
         ins->rex_w = 1;
         map_select = 1;
         opcodemap = vex_maps[map_select];
         ins->rex_r = ((ins->curbyte >> 7) & 1) ^ 1;
         // vvvv = (*pc >> 3) & 0xf;
         // l = (*pc >> 2) & 1;
         // pp = (*pc & 3);
         // prefix = vex_pp_encoding[pp];
         break;

      default:
         // The caller should not have called us with this prefix byte.
         km_abort("Called with unsupported opcode prefix 0x%x", prefix);
         break;
   }
   // Detect opcode maps we don't yet have support for.
   if (opcodemap == NULL) {
      km_warnx("Unsupported VEX/XOP opcode map, prefix 0x%x, map %d", prefix, map_select);
      return;
   }
   // Hop over the last byte of the prefix stuff.
   decode_consume_byte(vcpu, ins);
   if (ins->failed_addr != 0) {
      return;
   }
   // Get the opcode byte
   ins->opcode = ins->curbyte;
   decode_consume_byte(vcpu, ins);
   if (ins->failed_addr != 0) {
      return;
   }
   // If the opcode references memory, try to compute the fault address.
   unsigned short map_entry;
   if ((map_entry = find_opcode_in_map(opcodemap, ins->opcode)) != OPCODE_MAP_END) {
      // Check to see if this is a group opcode.
      // A group opcode uses the modrm.reg field to specify the operation the
      // instruction performs.
      if ((map_entry & ~OPCODE_MASK) >= OPCODE_GROUP_0 &&
          (map_entry & ~OPCODE_MASK) <= OPCODE_GROUP_31) {
         int i = (map_entry & OPCODE_GROUP_MASK) >> 8;
         unsigned char* group;
         group = (prefix == XOP_PREFIX) ? xop_groups[i] : vex_groups[i];
         unsigned char* p;
         for (p = group; *p != 0; p++) {
            if (((ins->curbyte >> 3) & 0x07) == *p) {
               break;
            }
         }
         if (*p == 0) {
            // This instruction references memory but we don't think it does.
            km_warnx("VEX/XOP group instruction faulted but doesn't reference memory: Prefix 0x%x, "
                     "map_select %d, Opcode 0x%x, modrm 0x%x",
                     prefix,
                     map_select,
                     ins->opcode,
                     ins->curbyte);
            return;
         }
      }
      // Found the opcode, compute reference memory address
      decode_modrm(vcpu, ins);
      km_infox(KM_TRACE_DECODE,
               "prefix 0x%x, opcode: 0x%02x, vex/xop map entry 0x%x, modrm: present:%d mode:%d "
               "reg:%d rm:%d",
               prefix,
               ins->opcode,
               map_entry,
               ins->modrm_present,
               ins->modrm_mode,
               ins->modrm_reg,
               ins->modrm_rm);
      decode_consume_byte(vcpu, ins);
      // SIB byte is handled in find_modrm_fault()
      find_modrm_fault(vcpu, ins);
   } else {
      // We are not expecting this instruction to reference memory.  Something is wrong.
      km_warnx("VEX/XOP instruction faulted but doesn't reference memory: Prefix 0x%x, map_select "
               "%d, Opcode 0x%x",
               prefix,
               map_select,
               ins->opcode);
   }
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
   if (ins.curbyte == VEX_3BYTE_PREFIX || ins.curbyte == XOP_PREFIX || ins.curbyte == VEX_2BYTE_PREFIX) {
      decode_vex_opcodes(vcpu, &ins);
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
