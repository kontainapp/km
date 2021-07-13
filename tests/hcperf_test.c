/*
 * Copyright 2021 Kontain Inc
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
 * Measure the hypercall performance penalty, by calling dummy hypercall <count> times, with <load>
 * computation in between. So `time hcperf_test 1000000 1` measures pure time for 1000000 hcalls,
 * while `time hcperf_test 100000 2000` measures 100000 hcalls with some compute in between.
 *
 * Mostly used for runs with perf and or stap.
 */

#include <err.h>
#include <stdlib.h>
#include <sys/prctl.h>

int main(int argc, char** argv)
{
   if (argc < 3) {
      err(1, "usage: hcperf_test count load");
   }
   long count = atol(argv[1]);
   long load = atol(argv[2]);
   for (long i = 0; i < count; i++) {
      volatile long x __attribute__((unused));
      for (int j = 0; j < load; j++) {
         x = j / (i + 1);
      }
      prctl(0, 0, 0, 0, 0);   // dummy_hcall in km
   }
   return (0);
}
