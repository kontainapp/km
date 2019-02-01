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
 * Helper in GDB test - controlled infinite loop. Not a part of auto test yet.
 *
 * Invoked in 2 shells, something like this:
 *
 * ./build/km/km -g 3333 tests/hello_loop.km
 * gdb -q -l 50000 --ex="target remote localhost:3333" -ex="b run_forever"--ex="c" hello_loop.km
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

static const char* msg1 = "Hello, I am a loop ";

void run_forever(void)
{
   long volatile run_count = LONG_MAX;
   const int step = 10000;

   printf("run (almost) forever , count=%ld\n", run_count);
   // run until someone changes the 'run'. If run is <=0 on start, run forever
   while (--run_count != 0) {
      if (run_count % step == 0) {
         printf("Another brick in the wall # %ld (%ld)\n", run_count / step, run_count);
      }
   }
}

int main()
{
   puts(msg1);
   run_forever();
   exit(0);
}
