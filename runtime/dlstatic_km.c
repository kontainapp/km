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
 * dl*() and dlsym() implementations which use statically linked stuff
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsd_queue.h"
#include "dlstatic_km.h"

typedef struct km_dl_lib {
   LIST_ENTRY(km_dl_lib) link;
} km_dl_lib_t;

LIST_HEAD(km_dlstatic_libs_list, km_dl_lib) registered = LIST_HEAD_INITIALIZER(registered);
typedef struct km_dlstatic_libs_list km_dlstatic_libs_list_t;

typedef struct km_dl_handle {
   LIST_ENTRY(km_dl_handle) link;
} km_dl_handle_t;

LIST_HEAD(km_dl_handles_list, km_dl_handle) handles = LIST_HEAD_INITIALIZER(km_dl_handles_list);
typedef struct km_dl_handles_list km_dl_handles_list_t;

void km_dl_register(km_dl_lib_reg_t* lib)
{
   printf("%s: registering %s TODO\n", __FUNCTION__, lib->name);
   km_dl_lib_t* elem = malloc(sizeof(*elem));
   LIST_INSERT_HEAD(&registered, elem, link);
}

/*
- on dlopen, try to open the .json and get a name from it (simple search for now), or do key=value later
- save in the list of dlopens, handle is & inside)
- dlsym
*/

void* dlopen(const char* file, int mode)
{
   // open fiscan registered
   return NULL;
}

int dladdr(const void* addr_arg, Dl_info* info)
{
   return 0;
}

void* dlsym(void* restrict p, const char* restrict s)
{
   return NULL;
}
