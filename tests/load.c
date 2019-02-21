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
 * Test loading larger program into KM memory, also write. Use exit value
 */
#include <unistd.h>
#include "syscall.h"

static char msg1[] = "Hello, world,";
static char pad[0x400000] = " padding";
static const char msg2[] = "after loading big array\n";
/*
 * To recompute the size, compile load.km then run make load_expected_size and replace the number
 * below with printed one.
 */
static const unsigned long size = 0x0000000000602020;

int main()
{
   int ret;
   unsigned int x;

   ret = write(1, msg1, sizeof(msg1) - 1);
   ret = write(1, pad, 1);
   ret = write(1, msg2, sizeof(msg2) - 1);
   if (ret)
      ;

   if ((x = syscall(SYS_brk, 0)) != size) {
      return 1;
   }
   return 0;
}
