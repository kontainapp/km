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
 */

#ifndef KM_COREDUMP_H_
#define KM_COREDUMP_H_

#include "km.h"
#include "x86_cpu.h"

/*
 * Kontain specific ELF note extensions for guest core files to enable snapshots.
 *
 * elf.h uses NT_* as a convention for note types. We conform to that in our new note
 * types. The structures are for KM convience, hence the 'km_' naming convention
 * is used.
 */

/*
 * KM specific per-VCPU state (NT_KM_VCPU)
 * for snapshot recovery
 */
typedef struct km_nt_vcpu {
   Elf64_Word vcpu_id;
   Elf64_Addr stack_top;
   Elf64_Addr guest_thr;
   Elf64_Addr set_child_tid;
   Elf64_Addr clear_child_tid;
   Elf64_Word on_sigaltstack;      //
   Elf64_Addr sigaltstack_sp;      //
   Elf64_Word sigaltstack_flags;   //
   Elf64_Off sigaltstack_size;     //
   Elf64_Addr mapself_base;        // delayed unmap address
   Elf64_Off mapself_size;         // and size
   /*
    * TODO: SIGMASK
    * TODO: Debug registers?
    */
} km_nt_vcpu_t;
#define NT_KM_VCPU 0x4b4d5052   // "KMPR"

/*
 * Description of original guest exec and dynlinker.
 * Used to recover km_guest and km_dynlinker in
 * snapshot recovery
 */
typedef struct km_nt_guest {
   Elf64_Addr load_adjust;
   Elf64_Ehdr ehdr;
   // Followed by PHDR list and filename
} km_nt_guest_t;
#define NT_KM_GUEST 0x4b4d4754       // "KMGT"
#define NT_KM_DYNLINKER 0x4b4d444c   // "KMDL"

typedef struct km_nt_file {
   Elf64_Word fd;
   Elf64_Word flags;   // open(2) flags
   Elf64_Word mode;    // file mode
   Elf64_Off position;
   // Followed by file name
} km_nt_file_t;
#define NT_KM_FILE 0x4b4d4644   // "KMFD"

// Core dump guest.
void km_dump_core(char* filename, km_vcpu_t* vcpu, x86_interrupt_frame_t* iframe);
void km_set_coredump_path(char* path);
char* km_get_coredump_path();
int km_add_note_header(char* buf, size_t length, char* owner, int type, size_t descsz);

#endif