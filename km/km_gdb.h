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
 *
 * gdb stub-related declarations for x86_64 gdb arch (which is the only one we support).
 */

#ifndef __KM_GDB_H__
#define __KM_GDB_H__

#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include "km.h"
#include "km_signal.h"

/* GDB breakpoint/watchpoint types */
typedef enum {
   /* Do not change these. The values have to match on the GDB client
    * side. */
   GDB_BREAKPOINT_SW = 0,
   GDB_BREAKPOINT_HW,
   GDB_WATCHPOINT_WRITE,
   GDB_WATCHPOINT_READ,
   GDB_WATCHPOINT_ACCESS,
   GDB_BREAKPOINT_MAX
} gdb_breakpoint_type_t;

#define GDB_SIGNONE (-1)
#define GDB_SIGFIRST 0   // the guest hasn't run yet

#define KM_TRACE_GDB "gdb"

extern int km_gdb_read_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t* len);
extern int km_gdb_write_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t len);
extern int km_gdb_enable_ss(void);
extern int km_gdb_disable_ss(void);
extern int km_gdb_add_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len);
extern int km_gdb_remove_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len);

/*
 * Registers layout for send/receve to gdb. Has to match what gdb expects.
 * see ../docs/gdb_regs.xml for mandatory registers layout info.
 */
typedef unsigned char fpu_reg_t[10];
struct __attribute__((__packed__)) km_gdb_regs {
   uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15;
   uint64_t rip;
   uint32_t eflags;
   uint32_t cs, ss, ds, es, fs, gs;
   fpu_reg_t fpr[8];   // 8 80 bit FPRs (ST0-ST7)

   // 8 32bit fpu controls
   uint32_t fctrl, fstat, ftag, fiseg, fioff, foseg, fooff, fop;
};

#define GDB_INTERRUPT_PKT 0x3   // aka ^C

typedef struct gdbstub_info {
   int port;                // Port the stub is listening for gdb client. 0 means NO GDB
   int sock_fd;             // socket to communicate to gdb client
   int session_requested;   // set to 1 when payload threads need to pause on exit
   bool stepping;           // single step mode (stepi)
   km_vcpu_t* gdb_vcpu;     // VCPU which GDB is asking us to work on.
   pthread_mutex_t gdbnotify_mutex;    // used to serialize concurrent attempts to
                                       // call km_gdb_notify_and_wait()
   int exit_reason;         // last KVM exit reason
   int signo;               // signal number
   pid_t sigthreadid;       // the id of the thread causing signo to be generated.
   // Note: we use km_vcpu_get_tid() as gdb payload thread id
} gdbstub_info_t;

extern gdbstub_info_t gdbstub;

static inline int km_gdb_port_get(void)
{
   return gdbstub.port;
}

static inline void km_gdb_port_set(int port)
{
   gdbstub.port = port;
}

static inline void km_gdb_enable(int port)
{
   km_gdb_port_set(port);
}

static inline int km_gdb_is_enabled(void)
{
   return km_gdb_port_get() != 0 ? 1 : 0;
}

static inline km_vcpu_t* km_gdb_vcpu_get(void)
{
   return gdbstub.gdb_vcpu;
}

static inline void km_gdb_vcpu_set(km_vcpu_t* vcpu)
{
   gdbstub.gdb_vcpu = vcpu;
}

static inline int km_fd_is_gdb(int fd)
{
   return (fd == gdbstub.sock_fd);
}

extern int km_gdb_wait_for_connect(const char* image_name);
extern void km_gdb_main_loop(km_vcpu_t* main_vcpu);
extern void km_gdb_fini(int ret);
extern void km_gdb_start_stub(char* const payload_file);
extern void km_gdb_notify_and_wait(km_vcpu_t* vcpu, int signo);
extern char* mem2hex(const unsigned char* mem, char* buf, size_t count);
extern void km_guest_mem2hex(km_gva_t addr, km_kma_t kma, char* obuf, int len);
extern int km_gdb_update_vcpu_debug(km_vcpu_t* vcpu, uint64_t unused);

#endif /* __KM_GDB_H__ */
