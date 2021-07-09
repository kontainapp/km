/*
 * Copyright 2021 Kontain Inc.
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
int __attribute__((noinline)) change_and_print(int i)
{
   i *= i;
   i = (i << 12) + var1;
   printf("gdb_test: %s returned %d\n", __FUNCTION__, i);
   return i;
}

/*
 * Another helper gdb tests */
static int __attribute__((noinline)) rand_func(int i)
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
   printf("gdb_test: n=%d m=%d, EXPECTED=%d\n", n, m, expected);
   assert(n == m);
   // EXPECTED value is put in var1 here for gdb to validate
   var1 = n;
   n = rand_func(n);
   n = change_and_print(n);
   printf("gdb_test: got 0x%x. Now done\n", n);
   var1 = 0;

   exit(0);
}
