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

// Functions that may be useful for more than one test in the bats tests.

/*
 * Return the limit on the size of the process id.
 * We should never see a pid with the returned value or larger.
 * If there is a failure -1 is returned.
 */
static inline pid_t get_pid_max(void)
{
   FILE* pidmaxfile = fopen("/proc/sys/kernel/pid_max", "r");
   if (pidmaxfile != NULL) {
      pid_t pidmax;
      if (fscanf(pidmaxfile, "%d", &pidmax) != 1) {
         pidmax = -1;
      }
      fclose(pidmaxfile);
      return pidmax;
   }
   return -1;
}
