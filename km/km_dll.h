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
 */

#ifndef KKM_DLL_H_
#define KKM_DLL_H_
#define _GNU_SOURCE
#include "km.h"
#include <dlfcn.h>

void km_dl_init();
void km_dl_fini();
uint64_t km_dlopen(km_vcpu_t *vcpu, char *pathname, int flags);
uint64_t km_dlclose(km_vcpu_t *vcpu, void *handle);
uint64_t km_dlsym(km_vcpu_t *vcpu, uint64_t handle, char *sym);
uint64_t km_dlerror(km_vcpu_t *vcpu, char *msg, size_t length);
//uint64_t km_dladdr(km_vcpu_t *vcpu, void *addr, Dl_info *info);
uint64_t km_dlinfo(km_vcpu_t *vcpu, void *handle, int request, void *info);
#endif