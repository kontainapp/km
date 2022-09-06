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
    * Machine state
    */
   Elf64_Addr brk;
   Elf64_Addr tbrk;
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
   Elf64_Off data;          // depends on file type.
   Elf64_Word datalength;   // bytes of pipe contents following filename
   // Followed by file name
   // Followed by data buffered in a pipe
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
   Elf64_Word datalength;   // number of bytes to write back to the
                            // write side of a socketpair.  The data
                            // bytes follow the protocol address of
                            // this note.
   // Protocol address follows
   // Data buffered in the "write side" of a socketpair follows
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
#define KM_NT_SKSTATE_ERROR 5

/*
 * Use a function so that we consistently roundup note related pieces in the rest of the code.
 *
 * Apparently elf note fields are supposed to be aligned on a 4 or 8 byte boundary.
 * See: https://groups.google.com/g/generic-abi/c/vT-_QVcckXo?pli=1
 * Which refers to: http://sco.com/developers/gabi/latest/ch5.pheader.html#note_section
 * and: http://www.netbsd.org/docs/kernel/elf-notes.html#note-format
 * So we align the length of data we add to the end of a note so the next sequential
 * note will be aligned properly.
 */
static inline size_t km_nt_chunk_roundup(size_t size)
{
   return roundup(size, 4);
}

static inline size_t km_nt_file_padded_size(const char* str)
{
   return km_nt_chunk_roundup(strlen(str) + 1);
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
#define NT_KM_EPOLLFD 0x4b4d4550   // "KMEP"

// eventfd
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
   Elf64_Addr restorer;
} km_nt_sighand_t;
#define NT_KM_SIGHAND 0x4b4d5348   // "KMSH" no null term

// Core dump guest.
typedef enum { KM_DO_CORE, KM_DO_SNAP } km_coredump_type_t;
void km_dump_core(char* filename,
                  km_vcpu_t* vcpu,
                  x86_interrupt_frame_t* iframe,
                  const char* label,
                  const char* description,
                  km_coredump_type_t dumptype);
void km_set_coredump_path(char* path);
char* km_get_coredump_path();
size_t km_note_header_size(char* owner);
int km_add_note_header(char* buf, size_t length, char* owner, int type, size_t descsz);

#endif
