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

/*
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
   GDB_SIGNAL_HUP = 1,            // SIGHUP
   GDB_SIGNAL_INT = 2,            // SIGINT
   GDB_SIGNAL_QUIT = 3,           // SIGQUIT
   GDB_SIGNAL_ILL = 4,            // SIGILL
   GDB_SIGNAL_TRAP = 5,           // SIGTRAP
   GDB_SIGNAL_ABRT = 6,           // SIGABRT
   GDB_SIGNAL_EMT = 7,            // SIGEMT
   GDB_SIGNAL_FPE = 8,            // SIGFPE
   GDB_SIGNAL_KILL = 9,           // SIGKILL
   GDB_SIGNAL_BUS = 10,           // SIGBUS
   GDB_SIGNAL_SEGV = 11,          // SIGSEGV
   GDB_SIGNAL_SYS = 12,           // SIGSYS
   GDB_SIGNAL_PIPE = 13,          // SIGPIPE
   GDB_SIGNAL_ALRM = 14,          // SIGALRM
   GDB_SIGNAL_TERM = 15,          // SIGTERM
   GDB_SIGNAL_URG = 16,           // SIGURG
   GDB_SIGNAL_STOP = 17,          // SIGSTOP
   GDB_SIGNAL_TSTP = 18,          // SIGTSTP
   GDB_SIGNAL_CONT = 19,          // SIGCONT
   GDB_SIGNAL_CHLD = 20,          // SIGCHLD
   GDB_SIGNAL_TTIN = 21,          // SIGTTIN
   GDB_SIGNAL_TTOU = 22,          // SIGTTOU
   GDB_SIGNAL_IO = 23,            // SIGIO
   GDB_SIGNAL_XCPU = 24,          // SIGXCPU
   GDB_SIGNAL_XFSZ = 25,          // SIGXFSZ
   GDB_SIGNAL_VTALRM = 26,        // SIGVTALRM
   GDB_SIGNAL_PROF = 27,          // SIGPROF
   GDB_SIGNAL_WINCH = 28,         // SIGWINCH
   GDB_SIGNAL_LOST = 29,          // SIGLOST
   GDB_SIGNAL_USR1 = 30,          // SIGUSR1
   GDB_SIGNAL_USR2 = 31,          // SIGUSR2
   GDB_SIGNAL_PWR = 32,           // SIGPWR
   GDB_SIGNAL_POLL = 33,          // SIGPOLL
   GDB_SIGNAL_WIND = 34,          // SIGWIND not available on linux
   GDB_SIGNAL_PHONE = 35,         // SIGPHONE not available on linux
   GDB_SIGNAL_WAITING = 36,       // SIGWAITING not available on linux
   GDB_SIGNAL_LWP = 37,           // SIGLWP not available on linux
   GDB_SIGNAL_DANGER = 38,        // SIGDANGER not available on linux
   GDB_SIGNAL_GRANT = 39,         // SIGGRANT not available on linux
   GDB_SIGNAL_RETRACT = 40,       // SIGRETRACT not available on linux
   GDB_SIGNAL_MSG = 41,           // SIGMSG not available on linux
   GDB_SIGNAL_SOUND = 42,         // SIGSOUND not available on linux
   GDB_SIGNAL_SAK = 43,           // SIGSAK not available on linux
   GDB_SIGNAL_PRIO = 44,          // SIGPRIO not available on linux
   GDB_SIGNAL_REALTIME_33 = 45,   // SIG33, "Real-time event 33"
   GDB_SIGNAL_REALTIME_34 = 46,   // SIG34, "Real-time event 34"
   GDB_SIGNAL_REALTIME_35 = 47,   // SIG35, "Real-time event 35"
   GDB_SIGNAL_REALTIME_36 = 48,   // SIG36, "Real-time event 36"
   GDB_SIGNAL_REALTIME_37 = 49,   // SIG37, "Real-time event 37"
   GDB_SIGNAL_REALTIME_38 = 50,   // "SIG38", "Real-time event 38"
   GDB_SIGNAL_REALTIME_39 = 51,   // "SIG39", "Real-time event 39"
   GDB_SIGNAL_REALTIME_40 = 52,   // "SIG40", "Real-time event 40"
   GDB_SIGNAL_REALTIME_41 = 53,   // "SIG41", "Real-time event 41"
   GDB_SIGNAL_REALTIME_42 = 54,   // "SIG42", "Real-time event 42"
   GDB_SIGNAL_REALTIME_43 = 55,   // "SIG43", "Real-time event 43"
   GDB_SIGNAL_REALTIME_44 = 56,   // "SIG44", "Real-time event 44"
   GDB_SIGNAL_REALTIME_45 = 57,   // "SIG45", "Real-time event 45"
   GDB_SIGNAL_REALTIME_46 = 58,   // "SIG46", "Real-time event 46"
   GDB_SIGNAL_REALTIME_47 = 59,   // "SIG47", "Real-time event 47"
   GDB_SIGNAL_REALTIME_48 = 60,   // "SIG48", "Real-time event 48"
   GDB_SIGNAL_REALTIME_49 = 61,   // "SIG49", "Real-time event 49"
   GDB_SIGNAL_REALTIME_50 = 62,   // "SIG50", "Real-time event 50"
   GDB_SIGNAL_REALTIME_51 = 63,   // "SIG51", "Real-time event 51"
   GDB_SIGNAL_REALTIME_52 = 64,   // "SIG52", "Real-time event 52"
   GDB_SIGNAL_REALTIME_53 = 65,   // "SIG53", "Real-time event 53"
   GDB_SIGNAL_REALTIME_54 = 66,   // "SIG54", "Real-time event 54"
   GDB_SIGNAL_REALTIME_55 = 67,   // "SIG55", "Real-time event 55"
   GDB_SIGNAL_REALTIME_56 = 68,   // "SIG56", "Real-time event 56"
   GDB_SIGNAL_REALTIME_57 = 69,   // "SIG57", "Real-time event 57"
   GDB_SIGNAL_REALTIME_58 = 70,   // "SIG58", "Real-time event 58"
   GDB_SIGNAL_REALTIME_59 = 71,   // "SIG59", "Real-time event 59"
   GDB_SIGNAL_REALTIME_60 = 72,   // "SIG60", "Real-time event 60"
   GDB_SIGNAL_REALTIME_61 = 73,   // "SIG61", "Real-time event 61"
   GDB_SIGNAL_REALTIME_62 = 74,   // "SIG62", "Real-time event 62"
   GDB_SIGNAL_REALTIME_63 = 75,   // "SIG63", "Real-time event 63"

   GDB_SIGNAL_CANCEL = 76,   // SIGCANCEL musl signal

   GDB_SIGNAL_REALTIME_32 = 77,     // SIG32, "Real-time event 32"
   GDB_SIGNAL_REALTIME_64 = 78,     // "SIG64", "Real-time event 64"
   GDB_SIGNAL_REALTIME_65 = 79,     // "SIG65", "Real-time event 65"
   GDB_SIGNAL_REALTIME_66 = 80,     // "SIG66", "Real-time event 66"
   GDB_SIGNAL_REALTIME_67 = 81,     // "SIG67", "Real-time event 67"
   GDB_SIGNAL_REALTIME_68 = 82,     // "SIG68", "Real-time event 68"
   GDB_SIGNAL_REALTIME_69 = 83,     // "SIG69", "Real-time event 69"
   GDB_SIGNAL_REALTIME_70 = 84,     // "SIG70", "Real-time event 70"
   GDB_SIGNAL_REALTIME_71 = 85,     // "SIG71", "Real-time event 71"
   GDB_SIGNAL_REALTIME_72 = 86,     // "SIG72", "Real-time event 72"
   GDB_SIGNAL_REALTIME_73 = 87,     // "SIG73", "Real-time event 73"
   GDB_SIGNAL_REALTIME_74 = 88,     // "SIG74", "Real-time event 74"
   GDB_SIGNAL_REALTIME_75 = 89,     // "SIG75", "Real-time event 75"
   GDB_SIGNAL_REALTIME_76 = 90,     // "SIG76", "Real-time event 76"
   GDB_SIGNAL_REALTIME_77 = 91,     // "SIG77", "Real-time event 77"
   GDB_SIGNAL_REALTIME_78 = 92,     // "SIG78", "Real-time event 78"
   GDB_SIGNAL_REALTIME_79 = 93,     // "SIG79", "Real-time event 79"
   GDB_SIGNAL_REALTIME_80 = 94,     // "SIG80", "Real-time event 80"
   GDB_SIGNAL_REALTIME_81 = 95,     // "SIG81", "Real-time event 81"
   GDB_SIGNAL_REALTIME_82 = 96,     // "SIG82", "Real-time event 82"
   GDB_SIGNAL_REALTIME_83 = 97,     // "SIG83", "Real-time event 83"
   GDB_SIGNAL_REALTIME_84 = 98,     // "SIG84", "Real-time event 84"
   GDB_SIGNAL_REALTIME_85 = 99,     // "SIG85", "Real-time event 85"
   GDB_SIGNAL_REALTIME_86 = 100,    // "SIG86", "Real-time event 86"
   GDB_SIGNAL_REALTIME_87 = 101,    // "SIG87", "Real-time event 87"
   GDB_SIGNAL_REALTIME_88 = 102,    // "SIG88", "Real-time event 88"
   GDB_SIGNAL_REALTIME_89 = 103,    // "SIG89", "Real-time event 89"
   GDB_SIGNAL_REALTIME_90 = 104,    // "SIG90", "Real-time event 90"
   GDB_SIGNAL_REALTIME_91 = 105,    // "SIG91", "Real-time event 91"
   GDB_SIGNAL_REALTIME_92 = 106,    // "SIG92", "Real-time event 92"
   GDB_SIGNAL_REALTIME_93 = 107,    // "SIG93", "Real-time event 93"
   GDB_SIGNAL_REALTIME_94 = 108,    // "SIG94", "Real-time event 94"
   GDB_SIGNAL_REALTIME_95 = 109,    // "SIG95", "Real-time event 95"
   GDB_SIGNAL_REALTIME_96 = 110,    // "SIG96", "Real-time event 96"
   GDB_SIGNAL_REALTIME_97 = 111,    // "SIG97", "Real-time event 97"
   GDB_SIGNAL_REALTIME_98 = 112,    // "SIG98", "Real-time event 98"
   GDB_SIGNAL_REALTIME_99 = 113,    // "SIG99", "Real-time event 99"
   GDB_SIGNAL_REALTIME_100 = 114,   // "SIG100", "Real-time event 100"
   GDB_SIGNAL_REALTIME_101 = 115,   // "SIG101", "Real-time event 101"
   GDB_SIGNAL_REALTIME_102 = 116,   // "SIG102", "Real-time event 102"
   GDB_SIGNAL_REALTIME_103 = 117,   // "SIG103", "Real-time event 103"
   GDB_SIGNAL_REALTIME_104 = 118,   // "SIG104", "Real-time event 104"
   GDB_SIGNAL_REALTIME_105 = 119,   // "SIG105", "Real-time event 105"
   GDB_SIGNAL_REALTIME_106 = 120,   // "SIG106", "Real-time event 106"
   GDB_SIGNAL_REALTIME_107 = 121,   // "SIG107", "Real-time event 107"
   GDB_SIGNAL_REALTIME_108 = 122,   // "SIG108", "Real-time event 108"
   GDB_SIGNAL_REALTIME_109 = 123,   // "SIG109", "Real-time event 109"
   GDB_SIGNAL_REALTIME_110 = 124,   // "SIG110", "Real-time event 110"
   GDB_SIGNAL_REALTIME_111 = 125,   // "SIG111", "Real-time event 111"
   GDB_SIGNAL_REALTIME_112 = 126,   // "SIG112", "Real-time event 112"
   GDB_SIGNAL_REALTIME_113 = 127,   // "SIG113", "Real-time event 113"
   GDB_SIGNAL_REALTIME_114 = 128,   // "SIG114", "Real-time event 114"
   GDB_SIGNAL_REALTIME_115 = 129,   // "SIG115", "Real-time event 115"
   GDB_SIGNAL_REALTIME_116 = 130,   // "SIG116", "Real-time event 116"
   GDB_SIGNAL_REALTIME_117 = 131,   // "SIG117", "Real-time event 117"
   GDB_SIGNAL_REALTIME_118 = 132,   // "SIG118", "Real-time event 118"
   GDB_SIGNAL_REALTIME_119 = 133,   // "SIG119", "Real-time event 119"
   GDB_SIGNAL_REALTIME_120 = 134,   // "SIG120", "Real-time event 120"
   GDB_SIGNAL_REALTIME_121 = 135,   // "SIG121", "Real-time event 121"
   GDB_SIGNAL_REALTIME_122 = 136,   // "SIG122", "Real-time event 122"
   GDB_SIGNAL_REALTIME_123 = 137,   // "SIG123", "Real-time event 123"
   GDB_SIGNAL_REALTIME_124 = 138,   // "SIG124", "Real-time event 124"
   GDB_SIGNAL_REALTIME_125 = 139,   // "SIG125", "Real-time event 125"
   GDB_SIGNAL_REALTIME_126 = 140,   // "SIG126", "Real-time event 126"
   GDB_SIGNAL_REALTIME_127 = 141,   // "SIG127", "Real-time event 127"

   GDB_SIGNAL_INFO = 142,   // SIGINFO, "Information request"

   GDB_SIGNAL_UNKNOWN = 143,   // NULL, "Unknown signal"

   GDB_SIGNAL_DEFAULT = 144,   // NULL, "Internal error: printing GDB_SIGNAL_DEFAULT"

   GDB_EXC_BAD_ACCESS = 145,        // "EXC_BAD_ACCESS", "Could not access memory"
   GDB_EXC_BAD_INSTRUCTION = 146,   // "EXC_BAD_INSTRUCTION", "Illegal instruction/operand"
   GDB_EXC_ARITHMETIC = 147,        // "EXC_ARITHMETIC", "Arithmetic exception"
   GDB_EXC_EMULATION = 148,         // "EXC_EMULATION", "Emulation instruction"
   GDB_EXC_SOFTWARE = 149,          // "EXC_SOFTWARE", "Software generated exception"
   GDB_EXC_BREAKPOINT = 150,        // "EXC_BREAKPOINT", "Breakpoint"

   GDB_SIGNAL_LIBRT = 151,   // "SIGLIBRT", "librt internal signal"

   GDB_SIGNAL_LAST = 152,   // none

} gdb_signal_number_t;

#define GDB_SIGNONE (-1)
#define GDB_SIGFIRST 0   // the guest hasn't run yet

// Some pseudo signals that will be beyond the range of valid gdb signals.
#define GDB_KMSIGNAL_BASE 1000   // arbitarily chosen large value
#define GDB_KMSIGNAL_KVMEXIT (GDB_KMSIGNAL_BASE + 20)
#define GDB_KMSIGNAL_THREADEXIT (GDB_KMSIGNAL_BASE + 21)
#define GDB_KMSIGNAL_DOFORK (GDB_KMSIGNAL_BASE + 22)
#define GDB_KMSIGNAL_EXEC2PROG (GDB_KMSIGNAL_BASE + 23)

#define KM_TRACE_GDB "gdb"

extern int km_gdb_read_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t* len);
extern int km_gdb_write_registers(km_vcpu_t* vcpu, uint8_t* reg, size_t len);
extern int km_gdb_enable_ss(void);
extern int km_gdb_disable_ss(void);
extern int km_gdb_add_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len);
extern int
km_gdb_remove_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len, int skip_hw_update);
extern int km_gdb_remove_all_breakpoints(int skip_hw_update);
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
   GDB_WAIT_FOR_ATTACH_UNSPECIFIED,   // no value selected
   GDB_DONT_WAIT_FOR_ATTACH,          // run the payload without waiting for gdb client attach
   GDB_WAIT_FOR_ATTACH_AT_DYNLINK,    // wait for client attach before running dynamic linker
                                      //  (if statically linked, wait at _start)
   GDB_WAIT_FOR_ATTACH_AT_START,      // wait for client attach before running _start (this
                                      //  let's the dynamic linker run)
} gdb_wait_for_attach_t;

typedef struct gdbstub_info {
   int port;               // Port the stub is listening for gdb client.
   int listen_socket_fd;   // listen for gdb client connections on this fd.
   int sock_fd;            // socket to communicate to gdb client
   uint8_t enabled;        // if true the gdb server is enabled and will be listening
                           //  on network port specified by port.
   gdb_wait_for_attach_t wait_for_attach;
   // if gdb server is enabled, should the gdb server wait
   //  for attach from the client before running the payload
   //  and where should it wait for attach.
   uint8_t gdb_client_attached;        // if true, gdb client is attached.
   int session_requested;              // set to 1 when payload threads need to pause on exit
   bool stepping;                      // single step mode (stepi)
   km_vcpu_t* gdb_vcpu;                // VCPU which GDB is asking us to work on.
   pthread_mutex_t notify_mutex;       // serialize calls to km_gdb_notify_and_wait()
   gdb_event_queue_t event_queue;      // queue of pending gdb events
   int exit_reason;                    // last KVM exit reason
   int send_threadevents;              // if non-zero send thread create and terminate events
   uint8_t send_exec_event;            // set by the exec target program to have exec event sent
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
extern const_string_t KM_GDB_CHILD_FORK_WAIT;

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
extern int km_gdb_update_vcpu_debug(km_vcpu_t* vcpu, void* unused);
extern void km_empty_out_eventfd(int fd);
extern int km_gdb_setup_listen(void);
extern void km_gdb_destroy_listen(void);
extern void km_gdb_accept_stop(void);
extern void km_gdb_attach_message(void);
extern void km_gdb_fork_reset(void);
extern void km_vcpu_resume_all(void);
extern int km_gdb_need_to_wait_for_client_connect(const char* envvarname);

#endif /* __KM_GDB_H__ */
