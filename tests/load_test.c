/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Test loading larger program into KM memory, also write. Use exit value
 */
#include <stdio.h>
#include <unistd.h>
#include "syscall.h"

static int print_size = 0;
static int shared_lib = 0;

static char msg1[] = "Hello, world,";
static char pad[0x400000] = " padding";
static const char msg2[] = "after loading big array\n";
/*
 * To recompute the size, compile load.km then run make load_expected_size and replace the number
 * below with printed one.
 */

static const unsigned long size[] = {0x0000000000606458, 0x0000000000606770, 0x0000000000606438, 0};
static const unsigned long shared_size[] = {0x292440, 0};

int main(int argc, char* argv[])
{
   int ret;
   unsigned long x;
   int c;

   while ((c = getopt(argc, argv, "ps")) != -1) {
      switch (c) {
         case 'p':
            print_size = 1;
            break;
         case 's':
            shared_lib = 1;
            break;
      }
   }

   ret = write(1, msg1, sizeof(msg1) - 1);
   ret = write(1, pad, 1);
   ret = write(1, msg2, sizeof(msg2) - 1);
   if (ret)
      ;

   x = syscall(SYS_brk, 0);
   if (print_size) {
      if (shared_lib) {
         fprintf(stderr, "(shared) size=0x%lx, expect:\n", x);
         for (int i = 0; shared_size[i] != 0; i++) {
            fprintf(stderr, "  0x%lx\n", shared_size[i]);
         }

      } else {
         fprintf(stderr, "(static) size=0x%lx, expect:\n", x);
         for (int i = 0; size[i] != 0; i++) {
            fprintf(stderr, "  0x%lx\n", size[i]);
         }
      }
   }
   if (shared_lib) {
      for (int i = 0; shared_size[i] != 0; i++) {
         if (x == shared_size[i]) {
            return 0;
         }
      }
   } else {
      for (int i = 0; size[i] != 0; i++) {
         if (x == size[i]) {
            return 0;
         }
      }
   }
   return 1;
}
