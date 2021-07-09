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
 * simple gets to test for behavior while in syscall
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
   const char prompt[] = "km> ";
   char* buffer = NULL;
   size_t len = 0;

   printf("%s", prompt);
   fflush(stdout);
   while ((len = getline(&buffer, &len, stdin)) != -1) {
      printf("Got len %ld:\n", len);
      printf("%s", prompt);
      fflush(stdout);
   }
   exit(0);
}
