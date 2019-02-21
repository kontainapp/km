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

#define LIBC_SYM_NAME "__libc"
#define PTHREAD_ENTRY_NAME "__pt_entry__"

/*
 * Description of the guest payload. Note these structures come from guest ELF and represent values
 * in guest address space. We'll need to convert them to monitor (KM) addresses to acces.
 */
typedef struct km_payload {
   Elf64_Ehdr km_ehdr;            // elf file header
   Elf64_Phdr* km_phdr;           // elf program headers
   Elf64_Addr km_libc;            // libc in payload program
   Elf64_Addr km_pthread_entry;   // pthread entry function in payload program
} km_payload_t;

extern km_payload_t km_guest;

int load_elf(const char* file);

#endif /* #ifndef __KM_H__ */
