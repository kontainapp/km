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
 * Test saying "hello world" via printf
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <stdio.h>
#include <stdlib.h>

static const char msg[] = "Hello,";

int main()
{
   char msg2[] = "world!\n";

   printf("%s %s", msg, msg2);
   exit(0);
}
