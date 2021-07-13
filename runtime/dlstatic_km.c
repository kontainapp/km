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

/*
 * dl*() and dlsym() implementations which use statically linked stuff. This code is linked with
 * payload. (Though we may place it via Monitor in the future)
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsd_queue.h"
#include "dlstatic_km.h"

#include "dynlink.h"   // musl internal, needed for dl_seterr
/*
 * Note: we should use musl's _dl_seterr dl_invalid_handle and dl_vseterr to set dl-related errors
 * so dlerror() properly spits them out.
 */

static char* const id_suffix = ".km.id";   // lib.id_suffix contants unique id for the given .so

typedef struct km_dl_lib {   // runtime linked libs info - filled from registrations
   char name[NAME_MAX];      // basename of original .so
   char id[NAME_MAX];        // unique id, aka munged_name
   int need_id;              // 1 if name is not unique in regisration, and dlopen() needs to use id
   km_dl_symbol_t* symbols;
   LIST_ENTRY(km_dl_lib) link;
} km_dl_lib_t;

typedef km_dl_lib_t* km_dl_handle_t;   // use address of lib entry as a handle for dlopen/dlsym

// Linked list of libs info
LIST_HEAD(km_dlstatic_libs_list, km_dl_lib) registered = LIST_HEAD_INITIALIZER(registered);
typedef struct km_dlstatic_libs_list km_dlstatic_libs_list_t;

// Pre-linked .so registration. Usually called from global constructor for the lib.
void km_dl_register(km_dl_lib_reg_t* reg)
{
   km_dl_lib_t* lib;

   km_dl_lib_t* elem = calloc(1, sizeof(*elem));
   elem->symbols = reg->symtable;
   strncpy(elem->id, reg->id, sizeof elem->id);
   strncpy(elem->name, reg->name, sizeof elem->name);

   // check if we MUST use unique id (or can get away with using the lib name)
   LIST_FOREACH (lib, &registered, link) {
      if (strcmp(lib->name, elem->name) == 0) {
         lib->need_id = 1;
         elem->need_id = 1;
         break;
      }
   }
   LIST_INSERT_HEAD(&registered, elem, link);
}

/*
 * get munged name for .so 'file'. munged_names should be a buffer with enough size.
 * returns 0 in success, -1 on failure
 */
static int get_munged_name(const char* file, char* munged_name)
{
   char buf[strlen(file) + strlen(id_suffix) + 1];
   FILE* stream;
   char* line;
   size_t len;

   // read unique id from file.km.id so we can locate correct registration
   // TODO - maintain per-KM "symptoms->id" mapping instead of this file.
   // For python, symptoms is just file path tail
   sprintf(buf, "%s%s", file, id_suffix);
   if ((stream = fopen(buf, "r")) == NULL) {
      perror(buf);
      return -1;
   }
   if (getline(&line, &len, stream) == -1) {
      perror(buf);
      strcpy(munged_name, "<not_found>");
   } else {
      strcpy(munged_name, line);
   }
   fclose(stream);
   return 0;
}

/*
 * dlopen() implementation for statically linked .so content
 *
 * TODO: do not ignore flags. Handle path and LD_LIBARARY_PATH/LD_PRELOAD preload.(See dllink.c in musl)
 */
void* dlopen(const char* file, int mode)
{
   const char* name;   // short name of the library to open
   km_dl_lib_t* lib = NULL;

   if (LIST_EMPTY(&registered)) {
      goto err;
   }
   if (file == NULL) {
      file = "python.orig";   // per agreement in python.km build - see buildenv, Makefile and link code
   }

   if ((name = rindex(file, '/')) != NULL) {
      name++;   // check if we know about .so with no path
   } else {
      name = file;
   }
   LIST_FOREACH (lib, &registered, link) {
      if (strcmp(lib->name, name) == 0) {
         if (lib->need_id == 0) {
            return lib;
         } else {
            break;
         }
      }
   }
   if (lib == LIST_END(&registered)) {
      goto err;
   }

   // we have to use the munged name to locate the lib
   char munged_name[NAME_MAX];
   if (get_munged_name(file, munged_name) < 0) {
      return NULL;
   }
   // locate registration using unique munged_name
   LIST_FOREACH (lib, &registered, link) {
      if (strcmp(lib->id, munged_name) == 0) {
         return lib;
      }
   }
err:
   warnx("dlopen: failed to find registration for %s, check if it was prelinked", file);
   return NULL;
}

// Implementatoin for musl's invalid handle - which is what they use for dlclose(0) as well
hidden int __dl_invalid_handle(void* handle)
{
   km_dl_lib_t* lib;

   LIST_FOREACH (lib, &registered, link) {
      if (handle == lib) {
         return 0;
      }
   }
   __dl_seterr("Invalid library handle %p", (void*)handle);
   return -1;   // handle not found
}

int dladdr(const void* addr_arg, Dl_info* info)
{
   // TODO: scan ALL registrations and see if the addr is matching a symbol
   // We probably need to get (and save) the size of each symtab referred symbol for this function
   // to work accurately
   __dl_seterr("dladdr(%p)no supported - returning NOT FOUND\n", addr_arg);
   return 0;
}

// Implementaion for musl's dlsym() wrapper
void* __dlsym(void* restrict handle, const char* restrict symbol, void* unused)
{
   km_dl_lib_t* lib;

   LIST_FOREACH (lib, &registered, link) {   // don't trust handle from payload; so search for it.
      if (handle != lib) {
         continue;
      }
      for (km_dl_symbol_t* s = lib->symbols; s->name != NULL; s++) {
         if (strncmp(s->name, symbol, NAME_MAX) == 0) {
            return s->addr;
         }
      }
      return NULL;   // handle is found, but symbol is not
   }
   return NULL;   // handle is nof found
}
