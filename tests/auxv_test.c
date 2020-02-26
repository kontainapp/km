/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#include <stdio.h>
#include <sys/auxv.h>

int main(int argc, char* argv[])
{
   char* auxval;

   auxval = (char*)getauxval(AT_PLATFORM);
   if (auxval == NULL) {
      printf("AT_PLATFORM is missing\n");
     return 1;
   } else {
      printf("AT_PLATFORM     %s\n", auxval);
   }
   auxval = (char*)getauxval(AT_EXECFN);
   if (auxval == NULL) {
      printf("AT_EXECFN is missing\n");
     return 1;
   } else {
      printf("AT_EXECFN       %s\n", auxval);
   }
   auxval = (char*)getauxval(AT_RANDOM);
   if (auxval == NULL) {
      printf("AT_RANDOM is missing\n");
     return 1;
   } else {
      printf("AT_RANDOM      ");
      for (int i = 0; i < 16; i++) {
         printf(" %02x", auxval[i] & 0xff);
      }
      printf("\n");
   }
   auxval = (char*)getauxval(AT_SECURE);
   printf("AT_SECURE       %ld\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_EGID);
   printf("AT_EGID         %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_GID);
   printf("AT_GID          %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_EUID);
   printf("AT_EUID         %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_UID);
   printf("AT_UID          %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_ENTRY);
   printf("AT_ENTRY        %p\n", auxval);
   auxval = (char*)getauxval(AT_FLAGS);
   printf("AT_FLAGS        0x%lx\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_BASE);
   printf("AT_BASE         %p\n", auxval);
   auxval = (char*)getauxval(AT_PHNUM);
   printf("AT_PHNUM        %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_PHENT);
   printf("AT_PHENT        %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_CLKTCK);
   printf("AT_CLKTCK       %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_PAGESZ);
   printf("AT_PAGESZ       %lu\n", (uint64_t)auxval);
   auxval = (char*)getauxval(AT_SYSINFO_EHDR);
   printf("AT_SYSINFO_EHDR %p\n", auxval);

   return 0;
}
