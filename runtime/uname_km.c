/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * KM runtime replacement for utime(2).
 * Since we pretty much return constants, there is no need for syscall/hypercall.
 * Note: these constants are visible in the payload.
 */

#include <string.h>
#include <sys/utsname.h>
#include "syscall.h"

int uname(struct utsname* uts)
{
   int ret = syscall(SYS_uname, uts);
   // Buffers are all for 65 bytes (hardcoded in musl, so we are good)
   strcpy(uts->sysname, "kontain-runtime");
   strcpy(uts->release, "1.0");
   strcpy(uts->version, "preview");
   strcpy(uts->machine, "kontain_VM");

   return ret;
}
