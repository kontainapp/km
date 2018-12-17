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

int socket(int domain, int type, int protocol)
{
   km_hc_args_t arg;

   arg.arg1 = domain;
   arg.arg2 = type;
   arg.arg3 = protocol;
   return km_hcall(KM_HC_SOCKET, &arg);
}
