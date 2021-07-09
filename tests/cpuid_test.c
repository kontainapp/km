/*
 * Copyright 2021 Kontain Inc.
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
 * print out CPU Vendor Id - should be 'Kontain.app' (km_cpu_init.c:cpu_vendor_id) in the Payload
 */

#include <cpuid.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
   unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

   char vendor[13];
   __get_cpuid(0, &eax, &ebx, &ecx, &edx);   // mov eax,0; cpuid
   memcpy(vendor, &ebx, 4);                  // copy EBX
   memcpy(vendor + 4, &edx, 4);              // copy EDX
   memcpy(vendor + 8, &ecx, 4);              // copy ECX
   vendor[12] = '\0';
   printf("My CPU vendorId is %s\n", vendor);
}
