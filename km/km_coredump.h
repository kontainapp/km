/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
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

#include <string.h>

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
 * String for Note 'owner'('name') field.
 * "KMSP" = KM Snapshot Protocol
 */
#define KM_NT_NAME "KMSP"

/*
 * Information about the KM monitor environment that
 * created the coredump.
 */
typedef struct km_nt_monitor {
   Elf64_Word monitor_type;
   Elf64_Word label_length;
   Elf64_Word description_length;
   /*
    * NULL terminated strings label and descrption follow
    */
} km_nt_monitor_t;
#define NT_KM_MONITOR 0x4b4d4d4e   // "KMMN" no null term

/*
 * monitor_type values
 */
#define KM_NT_MONITOR_TYPE_KVM 0
#define KM_NT_MONITOR_TYPE_KKM 1

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
   Elf64_Addr sigaltstack_sp;      //
   Elf64_Word sigaltstack_flags;   //
   Elf64_Off sigaltstack_size;     //
   Elf64_Addr mapself_base;        // delayed unmap address
   Elf64_Off mapself_size;         // and size
   Elf64_Addr hcarg;               // current value of km_hcarg[vcpu_id]
   Elf64_Half hypercall;
   Elf64_Half restart;
   /*
    * TODO: SIGMASK
    * TODO: Debug registers?
    */
   Elf64_Word fp_format;   // format of floating point data that follows
   /*
    * Floating point data follows
    */
} km_nt_vcpu_t;
#define NT_KM_VCPU 0x4b4d5052   // "KMPR" no null term

/*
 * fp_format values for km_nt_vcpu
 */
#define NT_KM_VCPU_FPDATA_NONE 0      /* No fp data follows */
#define NT_KM_VCPU_FPDATA_KVM_FPU 1   /* KVM_GET_FPU follows*/
#define NT_KM_VCPU_FPDATA_KVM_XSAVE 2 /* KVM_GET_XSAVE follows*/
#define NT_KM_VCPU_FPDATA_KKM_XSAVE 3 /* KKM_GET_XSAVE follows*/

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
#define NT_KM_GUEST 0x4b4d4754       // "KMGT" no null term
#define NT_KM_DYNLINKER 0x4b4d444c   // "KMDL" no null term

/*
 * Elf note record for open file.
 */
typedef struct km_nt_file {
   Elf64_Word size;    // Size of record
   Elf64_Word fd;      // Open fd number
   Elf64_Word how;     // How file was created
   Elf64_Word flags;   // open(2) flags
   Elf64_Word mode;    // file mode (includes type)
   /*
    * The contents of data depends on the file type
    *   __S_IFREG  - lseek position
    *   __S_IFDIR  - lseek position
    *   __S_IFIFO  - 'other' fd in pipe
    *   __S_ISOCK  - 'other' fd in socketpair
    */
   Elf64_Off data;   // depends on file type.
   // Followed by file name
} km_nt_file_t;
#define NT_KM_FILE 0x4b4d4644   // "KMFD" no null term

typedef struct km_nt_socket {
   Elf64_Word size;      // Size of record
   Elf64_Word fd;        // Open fd number
   Elf64_Word how;       // How socket was created
   Elf64_Word state;     // state of socket
   Elf64_Word backlog;   // listen backlog
   Elf64_Word domain;
   Elf64_Word type;
   Elf64_Word protocol;
   Elf64_Word other;   // 'other' fd for socketpair(2)
   Elf64_Word addrlen;
   // Address follows
} km_nt_socket_t;
#define NT_KM_SOCKET 0x4b4d534b   // "KMSK" no null term

// values for 'how' field
#define KM_NT_SKHOW_SOCKETPAIR 0
#define KM_NT_SKHOW_SOCKET 1
#define KM_NT_SKHOW_ACCEPT 2

#define KM_NT_SKSTATE_OPEN 0
#define KM_NT_SKSTATE_BIND 1
#define KM_NT_SKSTATE_LISTEN 2
#define KM_NT_SKSTATE_ACCEPT 3
#define KM_NT_SKSTATE_CONNECT 4

static inline size_t km_nt_file_padded_size(char* str)
{
   return roundup(strlen(str) + 1, 4);
}

// Single event on eventfd (epoll_create)
typedef struct km_nt_event {
   Elf64_Word fd;   // fd to monitor
   Elf64_Word event;
   Elf64_Xword data;
} km_nt_event_t;

// eventfd (epoll_create)
typedef struct km_nt_eventfd {
   Elf64_Word size;         // Size of record
   Elf64_Word fd;           // Open event fd
   Elf64_Word flags;        // flags
   Elf64_Word event_size;   // size of event records that follow
   Elf64_Word nevent;       // number of event records that follow
} km_nt_eventfd_t;
#define NT_KM_EVENTFD 0x4b4d4556   // "KMEV" no null term

/*
 * Elf note record for signal handler.
 */
typedef struct km_nt_sighand {
   Elf64_Word size;   // size of struct (for validation)
   Elf64_Word signo;
   Elf64_Addr handler;
   Elf64_Word flags;   // sigaction flags
   Elf64_Word mask;    // sigmask
} km_nt_sighand_t;
#define NT_KM_SIGHAND 0x4b4d5348   // "KMSH" no null term

// Core dump guest.
void km_dump_core(
    char* filename, km_vcpu_t* vcpu, x86_interrupt_frame_t* iframe, char* label, char* description);
void km_set_coredump_path(char* path);
char* km_get_coredump_path();
size_t km_note_header_size(char* owner);
int km_add_note_header(char* buf, size_t length, char* owner, int type, size_t descsz);

#endif
