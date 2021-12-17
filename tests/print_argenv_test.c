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
 * Print out the command line args and the environment.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv)
{
   for (int i = 0; i < argc; i++) {
      printf("argv[%d] = '%s'\n", i, argv[i]);
   }
   if (environ == NULL || environ[0] == NULL) {
      printf("No environment variables\n");
   }
   for (int i = 0; environ[i] != NULL; i++) {
      printf("env[%d] = '%s'\n", i, environ[i]);
   }
   exit(0);
}
