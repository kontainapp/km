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

static const unsigned long size[] = {0x0000000000606528,   // ?
                                     0x0000000000606770,   // ?
                                     0x0000000000607448,   // fedora 31, gcc 9
                                     0x0000000000607428,   // fedora 32, gcc 10
                                     0x0000000000608410,   // musl v1.2.2
                                     0x0000000000608420,   // musl v1.2.2, gcc 9 (fedora 31)
                                     0};
static const unsigned long shared_size[] = {0x292410, 0};

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
