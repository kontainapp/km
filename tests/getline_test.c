/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
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
