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
 * Simple test of env vars
 */
#undef _FORTIFY_SOURCE   // TODO : this is needed on Ubuntu; to make right we need to define _
//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ENV_VAR_SIZE (1024ul * 16)   // account for stuff like BASH_FUNC_

int main(int argc, char* argv[])
{
   extern char** environ;
   printf("Testing getenv/putenv,  environ = %p\n", environ);
   for (int i = 0; environ[i] != 0; i++) {
      char name[MAX_ENV_VAR_SIZE];   // var name bufer
      char* c = strchr(environ[i], '=');
      if (c == NULL) {   // emulate strchrnul()
         c = environ[i] + strlen(environ[i]);
      }
      strncpy(name, environ[i], c - environ[i]);
      name[c - environ[i]] = 0;
      char* v = getenv(name);
      printf("getenv: %s=%s\n", name, v);
   }
   exit(0);
}
