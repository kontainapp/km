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
 */

#include <unistd.h>
#include <stdlib.h>

static const char *msg = "Hello, world\n";

int main()
{
   size_t ret = write(1, msg, 14);
   // supress compiler warning about unused var
   if (ret)
      ;
   exit(17);
}
