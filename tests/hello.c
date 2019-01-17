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
 * Test 'write' and 'puts' by saying hello world.
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "syscall.h"

static const char *msg1 = "Hello, ";
static char msg2[0x1ff000] = "world!";

void *SYS_break(void *addr)
{
   return (void *)syscall(SYS_brk, addr);
}

int main()
{
   ssize_t ret;
   void *ptr, *ptr1;

   printf("break is %p\n", SYS_break(NULL));

   ret = write(1, msg1, strlen(msg1));
   puts(msg2);
   // suppress compiler warning about unused var
   if (ret)
      ;

   printf("break is %p\n", ptr = SYS_break(NULL));
   SYS_break((void *)0x30000000);
   printf("break is %p\n", ptr1 = SYS_break(NULL));
   assert((void *)0x30000000 == SYS_break(NULL));
   printf("break is %p\n", SYS_break(NULL));

   ptr1 -= 20;
   strcpy(ptr1, msg1);
   strcat(ptr1, msg2);
   printf("%s from far up the memory %p\n", (char *)ptr1, ptr1);

   SYS_break((void *)ptr);
   assert(ptr == SYS_break(NULL));
   printf("break is %p\n", SYS_break(NULL));

   // return magic '17' to validate it's passing all the way up
   // We'll test for '17' upstairs in tests
   exit(17);
}
