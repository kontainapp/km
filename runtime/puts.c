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
#include <string.h>
#include <stdio.h>

int puts(const char *s)
{
   size_t len = strlen(s);
   ssize_t ret = write(1, s, len);
   if (ret == EOF ) {
      return EOF;
   }

   return write(1, "\n", 1);
}
