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
 * Test brk() with different values
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include "syscall.h"

static void const *high_addr = (void *)0x30000000ul;
static void const *very_high_addr = (void *)(512 * 0x40000000ul);

void *SYS_break(void const *addr)
{
   return (void *)syscall(SYS_brk, addr);
}

int main()
{
   ssize_t ret;
   void *ptr, *ptr1;

   printf("break is %p\n", ptr = SYS_break(NULL));
   SYS_break(high_addr);
   printf("break is %p\n", ptr1 = SYS_break(NULL));
   assert(ptr1 == high_addr);

   ptr1 -= 20;
   strcpy(ptr1, "Hello, world");
   printf("%s from far up the memory %p\n", (char *)ptr1, ptr1);

   if (SYS_break(very_high_addr) != very_high_addr) {
      perror("Unable to set brk that high");
   } else {
      printf("break is %p\n", ptr1 = SYS_break(NULL));

      ptr1 -= 20;
      strcpy(ptr1, "Hello, world");
      printf("%s from even farer up the memory %p\n", (char *)ptr1, ptr1);
   }

   SYS_break((void *)ptr);
   assert(ptr == SYS_break(NULL));
   printf("break is %p\n", SYS_break(NULL));

   exit(0);
}
