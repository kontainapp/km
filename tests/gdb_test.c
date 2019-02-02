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
 * Helper for gdb test - giving gdb a chance to put  breakpoints, print values and the likes.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int var1 = 333;
static char* str1 = "I am here !";

static int print_str(char const* s, int i)
{
      printf("Got %s and %d\n", s, i);
      i *= i;
      i *= strlen(s);
      printf("and converted to %d\n", i);
      return i;
}

int main()
{
   int n = var1 * var1 * strlen(str1);
   assert(n == print_str(str1, var1));
   var1 = n;
   printf("Done\n");

   exit(0);
}
