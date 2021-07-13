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
