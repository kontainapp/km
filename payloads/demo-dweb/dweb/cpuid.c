/*
 * Copyright 2021 Kontain Inc
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
 * returns vendorID and processor - vendor_iod should be 'Kontain.app' (km_cpu_init.c:cpu_vendor_id)
 * in the Payload
 *
 * Really brute force for demo - returns static buffer ! And the code does manual offsets. Sorry
 */

#include <cpuid.h>
#include <stdio.h>
#include <string.h>

char* get_cpu_vendorid(void)
{
   static char vendor[13];
   unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

   __get_cpuid(0, &eax, &ebx, &ecx, &edx);   // mov eax,0; cpuid
   memcpy(vendor, &ebx, 4);                  // copy EBX
   memcpy(vendor + 4, &edx, 4);              // copy EDX
   memcpy(vendor + 8, &ecx, 4);              // copy ECX
   vendor[12] = '\0';
   return vendor;
}

char* get_cpu_proc(void)
{
   static char processor[49];   // 3 set of 4 regs 4 bytes each + 1
   unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

   __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
   memcpy(processor, &eax, 4);
   memcpy(processor + 4, &ebx, 4);
   memcpy(processor + 8, &ecx, 4);
   memcpy(processor + 12, &edx, 4);
   __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
   memcpy(processor + 16, &eax, 4);
   memcpy(processor + 16 + 4, &ebx, 4);
   memcpy(processor + 16 + 8, &ecx, 4);
   memcpy(processor + 16 + 12, &edx, 4);
   __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
   memcpy(processor + 32, &eax, 4);
   memcpy(processor + 32 + 4, &ebx, 4);
   memcpy(processor + 32 + 8, &ecx, 4);
   memcpy(processor + 32 + 12, &edx, 4);

   return processor;
}
