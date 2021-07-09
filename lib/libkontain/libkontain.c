/*
 * Copyright 2021 Kontain Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
