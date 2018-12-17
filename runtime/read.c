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
#include "km_hcalls.h"

ssize_t read(int fildes, void *buf, size_t nbyte)
{
   km_hc_args_t arg;

   arg.arg1 = fildes;
   arg.arg2 = (uint64_t)buf;
   arg.arg3 = nbyte;
   return km_hcall(KM_HC_READ, &arg);
}
