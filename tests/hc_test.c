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
 * Invoke argv[1] hypercall. Used to test unsupported hypercalls
 */

#include <stdlib.h>

int syscall(long n, long a1);

int main(int argc, char** argv)
{
   if (argc != 2) {
      exit(1);
   }

   syscall(atoi(argv[1]), 0);

   exit(0);
}
