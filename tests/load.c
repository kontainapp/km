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
static const unsigned long size = 0x603004;

int main()
{
   int ret;

   ret = write(1, msg1, sizeof(msg1));
   ret = write(1, pad, 1);
   ret = write(1, msg2, sizeof(msg2));
   if (ret)
      ;

   if (syscall(SYS_brk, 0) != size) {
      syscall(SYS_exit, 1);
   }
   syscall(SYS_exit, 0);
}
