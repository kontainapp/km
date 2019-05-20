/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
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

#include <gelf.h>
#include <stdint.h>

#define KM_LIBC_SYM_NAME "__libc"
#define KM_INT_HNDL_SYM_NAME "__km_interrupt_handler"
#define KM_TSD_SIZE_SYM_NAME "__pthread_tsd_size"
/*
 * Description of the guest payload. Note these structures come from guest ELF and represent values
 * in guest address space. We'll need to convert them to monitor (KM) addresses to acces.
 */
typedef struct km_payload {
   Elf64_Ehdr km_ehdr;       // elf file header
   Elf64_Phdr* km_phdr;      // elf program headers
   Elf64_Addr km_libc;       // libc in payload program
   Elf64_Addr km_handlers;   // interrupt/exception handler
   Elf64_Addr km_tsd_size;   // __pthread_tsd_size in the payload
} km_payload_t;

extern km_payload_t km_guest;

int km_load_elf(const char* file);

#endif /* #ifndef __KM_H__ */
