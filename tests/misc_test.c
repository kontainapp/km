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
 * A collection of misc runtime & API tests. Basically, small tests with no better home go here.
 */

#include <stdio.h>
#include <sys/utsname.h>

int main(int argc, char* argv[])
{
   struct utsname name;

   if (uname(&name) >= 0) {
      printf("sysname=%s\nnodename=%s\nrelease=%s\nversion=%s\nmachine=%s\n",
             name.sysname,
             name.nodename,
             name.release,
             name.version,
             name.machine);
   }
   return 0;
}