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
 * Test 'write' and 'puts' by saying hello world.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *msg1 = "Hello, ";
static const char *msg2 = "world!";

int main()
{
   ssize_t ret;

   ret = write(1, msg1, strlen(msg1));
   puts(msg2);
   // supress compiler warning about unused var
   if (ret)
      ;

   // return magic '17' to validate it's passing all the way up
   // We'll test for '17' upstairs in tests
   exit(17);

}
