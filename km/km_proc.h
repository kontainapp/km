/*
 * Copyright 2021 Kontain Inc
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

#ifndef __KM_PROC_H__
#define __KM_PROC_H__

static const_string_t PROC_SELF_MAPS = "/proc/self/maps";
static const_string_t PROC_SELF_FD = "/proc/self/fd/%d";
static const_string_t PROC_SELF_EXE = "/proc/self/exe";
static const_string_t PROC_SELF = "/proc/self";

static const_string_t PROC_PID_FD = "/proc/%u/fd/%%d";
static const_string_t PROC_PID_EXE = "/proc/%u/exe";
static const_string_t PROC_PID = "/proc/%u";

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
