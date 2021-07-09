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

/*
 * Typedef and externs for emulated payload dlopen/dlsym/dlinfo.
 * "Emulated" means the actual code is statically linked into the payload .km file and
 * run-time requests to dl*() functions are satisfied from in-payload info
 */

#ifndef _DLSTATIC_KM_H_
#define _DLSTATIC_KM_H_

// symbol name to actual address mapping
typedef struct {
   const char* const name;   // symbol name, untouched
   void** const addr;        // address of the actual symbol - filled in by static link
} km_dl_symbol_t;

// Individual .so library registration.
typedef struct {
   char* const name;   // original .so name (sans dirname)
   char* const id;   // unique id for the .so. Usually original .so name without suffix + md5 digest
   km_dl_symbol_t* const symtable;   // NULL terminated array of symbols exported by this .so
} km_dl_lib_reg_t;

// Each lib's global constructor need to call this to register the lib
extern void km_dl_register(km_dl_lib_reg_t*);

#endif /* _DLSTATIC_KM_H_ */
