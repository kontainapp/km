/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Definitions related to guest executable loaded in KM
 */
#ifndef __KM_ELF_H__
#define __KM_ELF_H__

#include "km.h"

#include <gelf.h>
#include <stdint.h>

#define KM_DLOPEN_SYM_NAME "dlopen"

#define KM_DYNLINKER_STR "__km_dynlink__"
/*
 * Description of the guest payload. Note these structures come from guest ELF and represent values
 * in guest address space. We'll need to convert them to monitor (KM) addresses to acces.
 */
typedef struct km_payload {
   Elf64_Ehdr km_ehdr;            // elf file header
   Elf64_Phdr* km_phdr;           // elf program headers
   Elf64_Addr km_dlopen;          // dlopen() address to find link_map chain
   Elf64_Addr km_load_adjust;     // elf->guest vaddr adjustment
   char* km_filename;             // elf file name
   Elf64_Addr km_interp_vaddr;    // interpreter name vaddr (if exist)
   Elf64_Off km_interp_len;       // interpreter name length (if exist)
   Elf64_Addr km_dynamic_vaddr;   // dynamic section
   Elf64_Off km_dynamic_len;      // and length (if exist)
   Elf64_Addr km_min_vaddr;       // minimum vaddr
} km_payload_t;

typedef struct km_tls_module {
   struct km_tls_module* next;
   void* image;
   size_t len;
   size_t size;
   size_t align;
   size_t offset;
} km_tls_module_t;

extern km_payload_t km_guest;
extern km_payload_t km_dynlinker;
extern char* km_dynlinker_file;
extern km_tls_module_t km_main_tls;

uint64_t km_load_elf(const char* file);

#endif /* #ifndef __KM_H__ */
