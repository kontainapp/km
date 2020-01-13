#include <stdio.h>
#include <time.h>

time_t __attribute__((noinline)) next_thru_this_function(void)
{
   return time(NULL);
}

time_t __attribute__((noinline)) step_into_this_function(void)
{
   return time(NULL);
}

int main(int argc, char* argv[])
{
   time_t one;
   time_t one2;
   time_t two;

   one = next_thru_this_function();
   one2 = next_thru_this_function();
   two = step_into_this_function();

   printf("Average time %ld\n", (one + one2 + two) / 3);

   return 0;
}
