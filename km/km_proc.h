/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#ifndef __KM_PROC_H__
#define __KM_PROC_H__

static const_string_t PROC_SELF_MAPS = "/proc/self/maps";
static const_string_t PROC_SELF_FD = "/proc/self/fd/%d";
static const_string_t PROC_SELF_EXE = "/proc/self/exe";

typedef struct maps_region {
   char* name_substring;   // caller supplies this, the rest is filled in if name is found
   uint64_t begin_addr;
   uint64_t end_addr;
   uint8_t allowed_access;   // PROT_{READ,WRITE,EXEC}
   uint8_t found;
} maps_region_t;

#define vvar_vdso_regions_count 2
const static int vvar_region_index = 0;
const static int vdso_region_index = 1;
extern km_gva_t km_vvar_vdso_base[2];
extern uint32_t km_vvar_vdso_size;

extern int km_find_maps_regions(maps_region_t* regions, uint32_t nregions);

#endif   // !defined(__KM_PROC_H__)
