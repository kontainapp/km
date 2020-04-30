/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
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
