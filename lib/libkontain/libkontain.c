/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include "km_hcalls.h"
#include "libkontain.h"

int snapshot(char* label, char* application_name, int snapshot_live)
{
   km_hc_args_t snapshot_args = {
       .arg1 = (uint64_t)label,
       .arg2 = (uint64_t)application_name,
       .arg3 = (uint64_t)snapshot_live,
   };
   km_hcall(HC_snapshot, &snapshot_args);

   return snapshot_args.hc_ret;
}

size_t snapshot_getdata(void* buffer, size_t count)
{
   km_hc_args_t snapshot_getdata_args = {.arg1 = (uint64_t)buffer, .arg2 = (uint64_t)count};
   km_hcall(HC_snapshot_getdata, &snapshot_getdata_args);

   return (size_t)snapshot_getdata_args.hc_ret;
}

size_t snapshot_putdata(void* buffer, size_t count)
{
   km_hc_args_t snapshot_putdata_args = {.arg1 = (uint64_t)buffer, .arg2 = (uint64_t)count};
   km_hcall(HC_snapshot_putdata, &snapshot_putdata_args);

   return (size_t)snapshot_putdata_args.hc_ret;
}
