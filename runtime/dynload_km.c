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
 * KM runtime replacement for dlopen and friends.
 * KM payloads are statically linked. Language runtimes like Python
 * use dlopen to import modules written in languages like C.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <err.h>
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "km_hcalls.h"

void* dlopen(const char* filename, int flags)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)filename;
   arg.arg2 = flags;
   km_hcall(HC_dlopen, &arg);
   return (void *)arg.hc_ret;
}

int dlclose(void* handle)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)handle;
   km_hcall(HC_dlclose, &arg);
   return arg.hc_ret;
}

void *dlsym(void *handle, const char *symbol)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)handle;
   arg.arg2 = (uintptr_t)symbol;
   km_hcall(HC_dlsym, &arg);
   return (void *)arg.hc_ret;
}

int dladdr(const void *addr, Dl_info *info)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)addr;
   arg.arg2 = (uintptr_t)info;
   km_hcall(HC_dladdr, &arg);
   return arg.hc_ret;
}

char *dlerror(void)
{
   static char error_msg[256];

   km_hc_args_t arg;
   arg.arg1 = (uintptr_t) error_msg;
   arg.arg2 = sizeof(error_msg);
   km_hcall(HC_dlerror, &arg);
   if (arg.hc_ret != 0) {
      return error_msg;
   }
   return NULL;
}

int dlinfo(void *handle, int request, void *info)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)handle;
   arg.arg1 = request;
   arg.arg3 = (uintptr_t)info;
   km_hcall(HC_dlinfo, &arg);
   return arg.hc_ret;
}

#if 0
int dl_iterate_phdr(
                 int (*callback) (struct dl_phdr_info *info,
                                  size_t size, void *data),
                 void *data)
{
   km_hc_args_t arg;
   arg.arg1 = (uintptr_t)callback;
   arg.arg2 = (uintptr_t)data;
   km_hcall(HC_dl_iterate_phdr, &arg);
   return arg.hc_ret;
} 
#endif