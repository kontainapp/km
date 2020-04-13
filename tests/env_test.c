/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Simple test of env vars
 */
#undef _FORTIFY_SOURCE   // TODO : this is needed on Ubuntu; to make right we need to define _
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ENV_VAR_SIZE (1024ul * 16)   // account for stuff like BASH_FUNC_

int main(int argc, char* argv[])
{
   printf("Testing getenv/putenv,  environ = %p\n", environ);
   for (int i = 0; environ[i] != 0; i++) {
      char name[MAX_ENV_VAR_SIZE];   // var name bufer
      char* c = strchrnul(environ[i], '=');
      strncpy(name, environ[i], c - environ[i]);
      name[c - environ[i]] = 0;
      char* v = getenv(name);
      printf("getenv: %s=%s\n", name, v);
   }
   exit(0);
}
