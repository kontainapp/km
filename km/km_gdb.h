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

#define GDB_DEFAULT_PORT 2159   // per /etc/services, this is gdbremote default

/* GDB breakpoint/watchpoint types */
typedef enum {
   /*
    * Do not change these. The values have to match the value of the first
    * argument supplied on the gdb 'z' and 'Z' commands in the remote
    * server protocol.
    */
   GDB_BREAKPOINT_SW = 0,
   GDB_BREAKPOINT_HW,
   GDB_WATCHPOINT_WRITE,
   GDB_WATCHPOINT_READ,
   GDB_WATCHPOINT_ACCESS,
   GDB_BREAKPOINT_MAX
} gdb_breakpoint_type_t;

/*
 * The following are the signal numbers that gdb understands.  Some
 * of these have values that are different from the equivalent linux
 * signal numbers.
 * These values are from include/gdb/signals.def which is part of the
 * gdb source tarball.
 * You know what happens when gdb changes these values!
 */
typedef enum {
   GDB_SIGNAL_HUP = 1,       // SIGHUP
   GDB_SIGNAL_INT = 2,       // SIGINT
   GDB_SIGNAL_QUIT = 3,      // SIGQUIT
   GDB_SIGNAL_ILL = 4,       // SIGILL
   GDB_SIGNAL_TRAP = 5,      // SIGTRAP
   GDB_SIGNAL_ABRT = 6,      // SIGABRT
   GDB_SIGNAL_EMT = 7,       // SIGEMT
   GDB_SIGNAL_FPE = 8,       // SIGFPE
   GDB_SIGNAL_KILL = 9,      // SIGKILL
   GDB_SIGNAL_BUS = 10,      // SIGBUS
   GDB_SIGNAL_SEGV = 11,     // SIGSEGV
   GDB_SIGNAL_SYS = 12,      // SIGSYS
   GDB_SIGNAL_PIPE = 13,     // SIGPIPE
   GDB_SIGNAL_ALRM = 14,     // SIGALRM
   GDB_SIGNAL_TERM = 15,     // SIGTERM
   GDB_SIGNAL_URG = 16,      // SIGURG
   GDB_SIGNAL_STOP = 17,     // SIGSTOP
   GDB_SIGNAL_TSTP = 18,     // SIGTSTP
   GDB_SIGNAL_CONT = 19,     // SIGCONT
   GDB_SIGNAL_CHLD = 20,     // SIGCHLD
   GDB_SIGNAL_TTIN = 21,     // SIGTTIN
   GDB_SIGNAL_TTOU = 22,     // SIGTTOU
   GDB_SIGNAL_IO = 23,       // SIGIO
   GDB_SIGNAL_XCPU = 24,     // SIGXCPU
   GDB_SIGNAL_XFSZ = 25,     // SIGXFSZ
   GDB_SIGNAL_VTALRM = 26,   // SIGVTALRM
   GDB_SIGNAL_PROF = 27,     // SIGPROF
   GDB_SIGNAL_WINCH = 28,    // SIGWINCH
   GDB_SIGNAL_LOST = 29,     // SIGLOST
   GDB_SIGNAL_USR1 = 30,     // SIGUSR1
   GDB_SIGNAL_USR2 = 31,     // SIGUSR2
   GDB_SIGNAL_PWR = 32,      // SIGPWR
   GDB_SIGNAL_POLL = 33,     // SIGPOLL
} gdb_signal_number_t;

#define GDB_SIGNONE (-1)
#define GDB_SIGFIRST 0   // the guest hasn't run yet

// Some pseudo signals that will be beyond the range of valid gdb signals.
#define GDB_SIGNAL_LAST 1000   // arbitarily chosen large value
#define GDB_KMSIGNAL_KVMEXIT (GDB_SIGNAL_LAST + 20)
#define GDB_KMSIGNAL_THREADEXIT (GDB_SIGNAL_LAST + 21)

#define KM_TRACE_GDB "gdb"

extern int km_gdb_read_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t* len);
extern int km_gdb_write_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t len);
extern int km_gdb_enable_ss(void);
extern int km_gdb_disable_ss(void);
extern int km_gdb_add_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len);
extern int km_gdb_remove_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len);
extern int km_gdb_remove_all_breakpoints(void);
extern int
km_gdb_find_breakpoint(km_gva_t trigger_addr, gdb_breakpoint_type_t* type, km_gva_t* addr, size_t* len);
extern void km_gdb_vcpu_state_init(km_vcpu_t*);

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

// Characters that must be escaped to appear in remote protocol messages
#define GDB_REMOTEPROTO_SPECIALCHARS "}$#*"

// our gdb server will only allow 32 concurrent open vfile handles
#define MAX_GDB_VFILE_OPEN_FD 32

#define GDB_VFILE_FD_FREE_SLOT -1
typedef struct gdbstub_vfile {
   int fd[MAX_GDB_VFILE_OPEN_FD];
   pid_t current_fs;   // we use the root directory for this process for opens
} gdbstub_vfile_t;

// Define the gdb event queue head structure.
TAILQ_HEAD(gdb_event_queue, gdb_event);
typedef struct gdb_event_queue gdb_event_queue_t;

typedef enum gdb_wait_for_attach {
   GDB_WAIT_FOR_ATTACH_UNSPECIFIED,  // no value selected
   GDB_DONT_WAIT_FOR_ATTACH,         // run the payload without waiting for gdb client attach
   GDB_WAIT_FOR_ATTACH_AT_DYNLINK,   // wait for client attach before running dynamic linker
                                     //  (if statically linked, wait at _start)
   GDB_WAIT_FOR_ATTACH_AT_START,     // wait for client attach before running _start (this
                                     //  let's the dynamic linker run)
} gdb_wait_for_attach_t;

typedef struct gdbstub_info {
   int port;                       // Port the stub is listening for gdb client.
   int listen_socket_fd;           // listen for gdb client connections on this fd.
   int sock_fd;                    // socket to communicate to gdb client
   uint8_t enabled;                // if true the gdb server is enabled and will be listening
                                   //  on network port specified by port.
   gdb_wait_for_attach_t wait_for_attach;
                                   // if gdb server is enabled, should the gdb server wait
                                   //  for attach from the client before running the payload
                                   //  and where should it wait for attach.
   uint8_t gdb_client_attached;    // if true, gdb client is attached.
   int session_requested;          // set to 1 when payload threads need to pause on exit
   bool stepping;                  // single step mode (stepi)
   km_vcpu_t* gdb_vcpu;            // VCPU which GDB is asking us to work on.
   pthread_mutex_t notify_mutex;   // serialize calls to km_gdb_notify_and_wait()
   gdb_event_queue_t event_queue;      // queue of pending gdb events
   int exit_reason;                    // last KVM exit reason
   int send_threadevents;              // if non-zero send thread create and terminate events
   uint8_t clientsup_multiprocess;     // gdb client can support multiprocess
   uint8_t clientsup_xmlregisters;     // gdb client can support xmlregisters
   uint8_t clientsup_qRelocInsn;       // gdb client can support qRelocInsn
   uint8_t clientsup_swbreak;          // gdb client can accept swbreak stop reason in replies
   uint8_t clientsup_hwbreak;          // gdb client can accept hwbreak stop reason in replies
   uint8_t clientsup_forkevents;       // gdb client can accept fork events
   uint8_t clientsup_vforkevents;      // gdb client can accept vfork events
   uint8_t clientsup_execevents;       // gdb client can accept exec event extensions
   uint8_t clientsup_vcontsupported;   // gdb client can send vCont and vCont? requests
   uint8_t clientsup_qthreadevents;    // gdb client can send QThreadEvents requests
   // Note: we use km_vcpu_get_tid() as gdb payload thread id
   gdbstub_vfile_t vfile_state;   // state for the vfile operations
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

static inline void km_gdb_enable(int enabled)
{
   gdbstub.enabled = enabled;
}

static inline int km_gdb_is_enabled(void)
{
   return gdbstub.enabled;
}

static inline int km_gdb_client_is_attached(void)
{
   return gdbstub.gdb_client_attached;
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

extern void km_gdbstub_init(void);
extern int km_gdb_wait_for_connect(const char* image_name);
extern void km_gdb_main_loop(km_vcpu_t* main_vcpu);
extern void km_gdb_fini(int ret);
extern void km_gdb_start_stub(char* const payload_file);
extern void km_gdb_notify(km_vcpu_t* vcpu, int signo);
extern char* mem2hex(const unsigned char* mem, char* buf, size_t count);
extern int km_guest_mem2hex(km_gva_t addr, km_kma_t kma, char* obuf, int len);
extern unsigned char* hex2mem(const char* buf, unsigned char* mem, size_t count);
extern int km_guest_hex2mem(const char* buf, size_t count, km_kma_t mem);
extern int km_gdb_update_vcpu_debug(km_vcpu_t* vcpu, uint64_t unused);
extern void km_empty_out_eventfd(int fd);
extern int km_gdb_setup_listen(void);
extern void km_gdb_destroy_listen(void);
extern void km_gdb_accept_stop(void);

#endif /* __KM_GDB_H__ */
