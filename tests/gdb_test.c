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

static int volatile var1 = 333;   // volatile to stop optimization

/*
 * expected value (see below for calculation)
 * should be in sync with cmd_for_test.gdb
 */
static const int expected = 454201677;

/*
 * Do some conversion and return the result.
 * This result later will be printed in gdb to as a test
 */
int change_and_print(int i)
{
   i *= i;
   i = (i << 12) + var1;
   printf("%s returned %d\n", __FUNCTION__, i);
   return i;
}

/*
 * Another helper gdb tests */
static int rand_func(int i)
{
   int volatile n = i;
   n = rand() * i;
   n++;
   return n;
}

int main()
{
   int n = ((var1 * var1) << 12) + var1;
   int m = change_and_print(var1);
   printf("n=%d m=%d, EXPECTED=%d\n", n, m, expected);
   assert(n == m);
   // EXPECTED value is put in var1 here for gdb to validate
   var1 = n;
   n = rand_func(n);
   n = change_and_print(n);
   printf("got 0x%x. Now done\n", n);
   var1 = 0;

   exit(0);
}
