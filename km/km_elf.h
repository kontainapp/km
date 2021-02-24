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
#include <sys/mman.h>

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
   const char* km_filename;       // elf file name
   Elf64_Addr km_interp_vaddr;    // interpreter name vaddr (if exist)
   Elf64_Off km_interp_len;       // interpreter name length (if exist)
   Elf64_Addr km_dynamic_vaddr;   // dynamic section
   Elf64_Off km_dynamic_len;      // and length (if exist)
   Elf64_Addr km_min_vaddr;       // minimum vaddr
} km_payload_t;

extern km_payload_t km_guest;
extern km_payload_t km_dynlinker;
extern const char* km_dynlinker_file;

// Open elf file descriptor
typedef struct km_elf {
   Elf* elf;
   int fd;
   GElf_Ehdr ehdr;
   const char* filename;
} km_elf_t;

/*
 * Translate ELF region protection mmap to mmap protection flag
 */
static inline int prot_elf_to_mmap(Elf64_Word p_flags)
{
   int flags = 0;
   if ((p_flags & PF_R) != 0) {
      flags |= PROT_READ;
   }
   if ((p_flags & PF_W) != 0) {
      flags |= PROT_WRITE;
   }
   if ((p_flags & PF_X) != 0) {
      flags |= PROT_EXEC;
   }
   return flags;
}

uint64_t km_load_elf(km_elf_t* e);
km_elf_t* km_open_elf_file(const char* filename);
void km_close_elf_file(km_elf_t* e);

#endif /* #ifndef __KM_H__ */
