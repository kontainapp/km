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
 * Helper in GDB test - controlled infinite loop. Not a part of auto test yet.
 *
 * Invoked in 2 shells, something like this:
 *
 * ./build/km/km -g 2159 tests/hello_loop.km
 * gdb -q -l 50000 --ex="target remote localhost:2159" -ex="b run_forever"--ex="c" hello_loop.km
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

static const char* const msg1 = "Hello, I am a loop ";

void run_forever(void)
{
   long volatile run_count = LONG_MAX;
   const int step = 200000000;   // brute force delay

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
