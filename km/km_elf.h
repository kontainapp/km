/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
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

#define KM_INT_HNDL_SYM_NAME "__km_handle_interrupt"
#define KM_SIG_RTRN_SYM_NAME "__km_sigreturn"
#define KM_CLONE_CHILD_SYM_NAME "__km_clone_run_child"
/*
 * Description of the guest payload. Note these structures come from guest ELF and represent values
 * in guest address space. We'll need to convert them to monitor (KM) addresses to acces.
 */
typedef struct km_payload {
   Elf64_Ehdr km_ehdr;           // elf file header
   Elf64_Phdr* km_phdr;          // elf program headers
   Elf64_Addr km_handlers;       // interrupt/exception handler
   Elf64_Addr km_sigreturn;      // signal trampoline function
   Elf64_Addr km_clone_child;    // clone child trampoline function
   Elf64_Addr km_load_adjust;    // elf->guest vaddr adjustment
   char *km_filename;
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
extern km_tls_module_t km_main_tls;

uint64_t km_load_elf(const char* file);

#endif /* #ifndef __KM_H__ */
