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
 * Test saying "hello world" via printf
 */
#include <stdio.h>
#include <stdlib.h>

static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";

   printf("%s %s\n", msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   exit(0);
}
