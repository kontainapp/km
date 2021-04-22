// Helper to look at CPUID details. Does not run as test (yet)
//
// Based on: https://gist.github.com/macton/4dd5fec2113be284796e thus no Kontain header
//
// See: Intel Intrinsics Guide  https://software.intel.com/sites/landingpage/IntrinsicsGuide/
// See: CPUID Explorer          http://www.flounder.com/cpuid_explorer1.htm
// See: Playing with cpuid      http://newbiz.github.io/cpp/2010/12/20/Playing-with-cpuid.html
// See: MSDN __cpuid, __cpuidex http://msdn.microsoft.com/en-us/library/hskdteyh.aspx
// See: Notes on MMX/SSE and a searchable table of intrinsics.
// http://www.taffysoft.com/pages/20120418-01.html See: AMD CPUID Specification
// http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2012/10/25481.pdf

#if defined(__GNUC__)
#include <cpuid.h>
#include <stdint.h>
#elif defined(_WIN32)
#include <intrin.h>
typedef unsigned __int32 uint32_t;
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void cpuid(uint32_t op, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
{
#if defined(__GNUC__)
   __get_cpuid(op, eax, ebx, ecx, edx);
#elif defined(_WIN32)
   // MSVC provides a __cpuid function
   int regs[4];
   __cpuid(regs, op);
   *eax = (uint32_t)regs[0];
   *ebx = (uint32_t)regs[1];
   *ecx = (uint32_t)regs[2];
   *edx = (uint32_t)regs[3];
#endif
}

/*
 * have a local macro to support subop
 * cpuid.h implementation of __get_cpuid does not support subops
 */
void cpuid_with_subop(uint32_t op, uint32_t subop, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
{
   __asm__ __volatile__ ("cpuid\n\t"
      : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
      : "a" (op), "c" (subop));
}

typedef union EBChar4 {
   uint32_t u32;
   char c[4];
} EBChar4;

void print_feature(uint32_t test, const char* name)
{
   printf("%s - %s\n", (test) ? "YES" : "NO ", name);
}

void print_ebchar4(uint32_t a)
{
   EBChar4 a_c;
   a_c.u32 = a;
   for (int i = 0; i <= 3; i++) {
      if (isprint(a_c.c[i])) {
         putc(a_c.c[i], stdout);
      }
   }
}

int main(void)
{
   uint32_t eax = 0;
   uint32_t ebx = 0;
   uint32_t ecx = 0;
   uint32_t edx = 0;

   cpuid(0, &eax, &ebx, &ecx, &edx);
   uint32_t ids = eax;

   cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
   uint32_t ex_ids = eax;

   {
      // On AMD, 0x80000000 also returns Vendor ID; On Intel 0x80000000 is empty.
      cpuid(0x00000000, &eax, &ebx, &ecx, &edx);
      printf("Vendor    = ");
      print_ebchar4(ebx);
      print_ebchar4(edx);
      print_ebchar4(ecx);
      printf("\n");
   }
   if (ex_ids >= 0x80000004) {
      printf("Processor = ");
      cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
      print_ebchar4(eax);
      print_ebchar4(ebx);
      print_ebchar4(ecx);
      print_ebchar4(edx);
      cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
      print_ebchar4(eax);
      print_ebchar4(ebx);
      print_ebchar4(ecx);
      print_ebchar4(edx);
      cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
      print_ebchar4(eax);
      print_ebchar4(ebx);
      print_ebchar4(ecx);
      print_ebchar4(edx);
      printf("\n");
   }
   printf("\n");

   if (ids >= 1) {
      cpuid(1, &eax, &ebx, &ecx, &edx);
      print_feature(edx & (1 << 0), "FPU           (Floating-point Unit on-chip)");
      print_feature(edx & (1 << 1), "VME           (Virtual Mode Extension)");
      print_feature(edx & (1 << 2), "DE            (Debugging Extension)");
      print_feature(edx & (1 << 3), "PSE           (Page Size Extension)");
      print_feature(edx & (1 << 4), "TSC           (Time Stamp Counter)");
      print_feature(edx & (1 << 5), "MSR           (Model Specific Registers)");
      print_feature(edx & (1 << 6), "PAE           (Physical Address Extension)");
      print_feature(edx & (1 << 7), "MCE           (Machine Check Exception)");
      print_feature(edx & (1 << 8), "CX8           (CMPXCHG8 Instructions)");
      print_feature(edx & (1 << 9), "APIC          (On-chip APIC hardware)");
      print_feature(edx & (1 << 11), "SEP           (Fast System Call)");
      print_feature(edx & (1 << 12), "MTRR          (Memory type Range Registers)");
      print_feature(edx & (1 << 13), "PGE           (Page Global Enable)");
      print_feature(edx & (1 << 14), "MCA           (Machine Check Architecture)");
      print_feature(edx & (1 << 15), "CMOV          (Conditional Move Instruction)");
      print_feature(edx & (1 << 16), "PAT           (Page Attribute Table)");
      print_feature(edx & (1 << 17), "PSE36         (36bit Page Size Extension");
      print_feature(edx & (1 << 18), "PSN           (Processor Serial Number)");
      print_feature(edx & (1 << 19), "CLFSH         (CFLUSH Instruction)");
      print_feature(edx & (1 << 21), "DS            (Debug Store)");
      print_feature(edx & (1 << 22), "ACPI          (Thermal Monitor & Software Controlled Clock)");
      print_feature(edx & (1 << 23), "MMX           (Multi-Media Extension)");
      print_feature(edx & (1 << 24), "FXSR          (Fast Floating Point Save & Restore)");
      print_feature(edx & (1 << 25), "SSE           (Streaming SIMD Extension 1)");
      print_feature(edx & (1 << 26), "SSE2          (Streaming SIMD Extension 2)");
      print_feature(edx & (1 << 27), "SS            (Self Snoop)");
      print_feature(edx & (1 << 28), "HTT           (Hyper Threading Technology)");
      print_feature(edx & (1 << 29), "TM            (Thermal Monitor)");
      print_feature(edx & (1 << 31), "PBE           (Pend Break Enabled)");
      print_feature(ecx & (1 << 0), "SSE3          (Streaming SMD Extension 3)");
      print_feature(ecx & (1 << 3), "MW            (Monitor Wait Instruction");
      print_feature(ecx & (1 << 4), "CPL           (CPL-qualified Debug Store)");
      print_feature(ecx & (1 << 5), "VMX           (Virtual Machine Extensions)");
      print_feature(ecx & (1 << 7), "EST           (Enchanced Speed Test)");
      print_feature(ecx & (1 << 8), "TM2           (Thermal Monitor 2)");
      print_feature(ecx & (1 << 9), "SSSE3         (Supplemental Streaming SIMD Extensions 3)");
      print_feature(ecx & (1 << 10), "L1            (L1 Context ID)");
      print_feature(ecx & (1 << 12), "FMA3          (Fused Multiply-Add 3-operand Form)");
      print_feature(ecx & (1 << 13), "CAE           (Compare And Exchange 16B)");
      print_feature(ecx & (1 << 19), "SSE41         (Streaming SIMD Extensions 4.1)");
      print_feature(ecx & (1 << 20), "SSE42         (Streaming SIMD Extensions 4.2)");
      print_feature(ecx & (1 << 23),
                    "POPCNT        (Advanced Bit Manipulation - Bit Population Count Instruction)");
      print_feature(ecx & (1 << 25), "AES           (Advanced Encryption Standard)");
      print_feature(ecx & (1 << 28), "AVX           (Advanced Vector Extensions)");
      print_feature(ecx & (1 << 30), "RDRAND        (Random Number Generator)");
   }
   if (ids >= 7) {
      cpuid_with_subop(7, 0, &eax, &ebx, &ecx, &edx);
      print_feature(ebx & (1 << 5), "AVX2          (Advanced Vector Extensions 2)");
      print_feature(ebx & (1 << 3), "BMI1          (Bit Manipulations Instruction Set 1)");
      print_feature(ebx & (1 << 8), "BMI2          (Bit Manipulations Instruction Set 2)");
      print_feature(ebx & (1 << 19),
                    "ADX           (Multi-Precision Add-Carry Instruction Extensions)");
      print_feature(ebx & (1 << 16),
                    "AVX512F       (512-bit extensions to Advanced Vector Extensions Foundation)");
      print_feature(ebx & (1 << 26),
                    "AVX512PFI     (512-bit extensions to Advanced Vector Extensions Prefetch "
                    "Instructions)");
      print_feature(ebx & (1 << 27),
                    "AVX512ERI     (512-bit extensions to Advanced Vector Extensions Exponential "
                    "and Reciprocal Instructions)");
      print_feature(ebx & (1 << 28),
                    "AVX512CDI     (512-bit extensions to Advanced Vector Extensions Conflict "
                    "Detection Instructions)");
      print_feature(ebx & (1 << 29), "SHA           (Secure Hash Algorithm)");
   }
   if (ex_ids >= 0x80000001) {
      cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
      print_feature(edx & (1 << 29), "X64           (64-bit Extensions/Long mode)");
      print_feature(ecx & (1 << 5),
                    "LZCNT         (Advanced Bit Manipulation - Leading Zero Bit Count "
                    "Instruction)");
      print_feature(ecx & (1 << 6), "SSE4A         (Streaming SIMD Extensions 4a)");
      print_feature(ecx & (1 << 16), "FMA4          (Fused Multiply-Add 4-operand Form)");
      print_feature(ecx & (1 << 11), "XOP           (Extended Operations)");
      print_feature(ecx & (1 << 21), "TBM           (Trailing Bit Manipulation Instruction)");
      print_feature(ecx & (1 << 15), "LWP           (Light Weight Profiling Support)");
      print_feature(ecx & (1 << 13), "WDT           (Watchdog Timer Support)");
      print_feature(ecx & (1 << 10), "IBS           (Instruction Based Sampling)");
      print_feature(ecx & (1 << 8), "3DNOWPREFETCH (PREFETCH and PREFETCHW instruction support)");
      print_feature(ecx & (1 << 7), "MISALIGNSSE   (Misaligned SSE mode)");
      print_feature(ecx & (1 << 2), "SVM           (Secure Virtual Machine)");
      print_feature(ecx & (1 << 0), "LAHFSAHF      (LAHF and SAHF instruction support in 64-bit mode)");
      print_feature(edx & (1 << 26), "pdpe1g        (1G pages support)");
   }
   return 0;
}
