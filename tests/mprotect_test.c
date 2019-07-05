/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Test reaching out above brk to see if mprotect catches it.
 * No arg - access to memory above brk in the code
 * -w arg - access to memory above brk using write syscall
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

static char pad[0x400000] = " padding";
void inline IGUR()
{
} /* Ignore GCC Unused Result */

int main(int argc, char** argv)
{
   int c;
   int wr = 0;
   char* ptr = (char*)0x620000;   // 2MB start + 4MB pad + a little bit (less than 1MB) for code

   while ((c = getopt(argc, argv, "w")) != -1) {
      switch (c) {
         case 'w':
            wr = 1;
            break;
      }
   }

   if (wr != 0) {
      if (write(1, ptr, 1024) < 0 && errno == EFAULT) {
         exit(1);
      }
      IGUR(write(1, pad, 1024));   // to keep pad in
   } else {
      printf("%s", ptr);
   }
   exit(0);
}
