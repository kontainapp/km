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
 * Based on musl src/ldso/dl_iterate_phdr.c
 */

#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include "greatest/greatest.h"

struct dl_phdr_info buffer[100];
int info_idx;

static int callback(struct dl_phdr_info* info, size_t size, void* data)
{
   char* type;

   printf("Name: \"%s\" (%d segments)\n", info->dlpi_name, info->dlpi_phnum);
   for (int j = 0; j < info->dlpi_phnum; j++) {
      switch (info->dlpi_phdr[j].p_type) {
         case PT_LOAD:
            type = "PT_LOAD";
            break;
         case PT_DYNAMIC:
            type = "PT_DYNAMIC";
            break;
         case PT_INTERP:
            type = "PT_INTERP";
            break;
         case PT_NOTE:
            type = "PT_NOTE";
            break;
         case PT_PHDR:
            type = "PT_PHDR";
            break;
         case PT_TLS:
            type = "PT_TLS";
            break;
         case PT_GNU_EH_FRAME:
            type = "PT_GNU_EH_FRAME";
            break;
         case PT_GNU_STACK:
            type = "PT_GNU_STACK";
            break;
         case PT_GNU_RELRO:
            type = "PT_GNU_RELRO";
            break;
         default:
            type = NULL;
            break;
      }
      printf("    %2d: [%14p; memsz:%7lx] flags: 0x%x; ",
             j,
             (void*)(info->dlpi_addr + info->dlpi_phdr[j].p_vaddr),
             info->dlpi_phdr[j].p_memsz,
             info->dlpi_phdr[j].p_flags);
      if (type != NULL) {
         printf("%s\n", type);
      } else {
         printf("[other (0x%x)]\n", info->dlpi_phdr[j].p_type);
      }
   }
   buffer[info_idx++] = *info;
   return 0;
}

TEST test(void)
{
   uint64_t phdr, phent;
   printf("PHDR = 0x%lx, size = 0x%lx\n", phdr = getauxval(AT_PHDR), phent = getauxval(AT_PHENT));

   dl_iterate_phdr(callback, NULL);

   ASSERT_EQ(0x200040, phdr);
   ASSERT_EQ(0x38, phent);

   ASSERT_EQ(1, info_idx);
   ASSERT_EQ(buffer[0].dlpi_phnum, 7);
   ASSERT_EQ(buffer[0].dlpi_phdr[0].p_type, PT_LOAD);
   ASSERT_EQ(buffer[0].dlpi_phdr[0].p_flags, 4);
   ASSERT_EQ(buffer[0].dlpi_phdr[0].p_vaddr, 0x200000);

   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args
   // greatest_set_verbosity(1);

   /* Tests can be run as suites, or directly. Lets run directly. */
   RUN_TEST(test);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}
