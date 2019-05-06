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