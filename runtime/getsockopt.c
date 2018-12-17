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
#include <sys/socket.h>

#include "km_hcalls.h"

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
   km_hc_args_t arg;

   arg.arg1 = fd;
   arg.arg2 = level;
   arg.arg3 = optname;
   arg.arg4 = (uint64_t)optval;
   arg.arg5 = (uint64_t)optlen;
   return km_hcall(KM_HC_GETSOCKOPT, &arg);
}
