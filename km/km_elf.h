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

#define KM_LIBC_SYM_NAME "__libc"
#define KM_THR_START_SYM_NAME "__start_thread__"
#define KM_INT_HNDL_SYM_NAME "__km_handle_interrupt"
#define KM_TSD_SIZE_SYM_NAME "__pthread_tsd_size"
#define KM_PCREATE_SYM_NAME "pthread_create"
#define KM_SIG_RTRN_SYM_NAME "__km_sigreturn"
/*
 * Description of the guest payload. Note these structures come from guest ELF and represent values
 * in guest address space. We'll need to convert them to monitor (KM) addresses to acces.
 */
typedef struct km_payload {
   Elf64_Ehdr km_ehdr;           // elf file header
   Elf64_Phdr* km_phdr;          // elf program headers
   Elf64_Addr km_libc;           // libc in payload program
   Elf64_Addr km_start_thread;   // thread start wrapper
   Elf64_Addr km_handlers;       // interrupt/exception handler
   Elf64_Addr km_tsd_size;       // __pthread_tsd_size in the payload
   Elf64_Addr km_sigreturn;      // signal trampoline function
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

int km_load_elf(const char* file);

#endif /* #ifndef __KM_H__ */
