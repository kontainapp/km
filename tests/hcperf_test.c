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
      volatile long x;
      for (int j = 0; j < load; j++) {
         x = j / (i + 1);
      }
      prctl(0, 0, 0, 0, 0);   // dummy_hcall in km
   }
   return (0);
}