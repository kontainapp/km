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
