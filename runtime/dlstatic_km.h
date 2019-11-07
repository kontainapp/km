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
 * Typedef and externs for emulated payload dlopen/dlsym/dlinfo.
 * "Emulated" means the actual code is statically linked into the payload .km file and
 * run-time requests to dl*() functions are satisfied from in-payload info
 */

#ifndef _DLSTATIC_KM_H_
#define _DLSTATIC_KM_H_

// a symbol that was defined in original .so file
typedef struct {
   const char* const name;   // symbol name, untouched
   void** const addr;        // address of the actual symbol - filled in by static link
} km_dl_symbol_t;

// Individual .so library registration.
typedef struct {
   const char* const name;   // Uniq name identifying the .so. Usually original .so name + md5 digest
   const km_dl_symbol_t* const symtable;   // NULL terminated array of symbols exported by this .so
} km_dl_lib_reg_t;

// Each lib's global constructor need to call this to register the lib
extern void km_dl_register(km_dl_lib_reg_t*);

#endif /* _DLSTATIC_KM_H_ */