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
 * Implementation of the GDB Remote Serial Protocol mandatory parts.
 * See https://sourceware.org/gdb/onlinedocs/gdb/Overview.html
 */

/*
 * This file uses some code from Solo5 tenders/hvt/km_module_gdb.c
 *
 * Copyright (c) 2015-2018 Contributors as noted in the AUTHORS file
 *
 * This file is part of Solo5, a sandboxed execution environment.
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"

/*
 * A note about multi threaded debugging.
 * To support multi-threaded debugging the gdb server (us) must support the vCont? and vCont
 * requests from the gdb client.
 * To let the gdb client know we support these things, we must support the qSupported packet
 * in which we tell the gdb client that vCont is supported (vContSupported).
 * I have found that if you want the vCont:c option to be supported you must also support
 * vCont:CXX (which allows continuing with a signal).
 * To support xml thread lists, the gdb server will reply to qSupported with qXfer:threads:read+
 */

/*
 * A Structure to hold a snapshot of the event that is waking the gdb server up.
 */
struct gdb_event {
   int signo;
   int sigthreadid;
   int exit_reason;
};
typedef struct gdb_event gdb_event_t;

gdbstub_info_t gdbstub = {   // GDB global info
    .sock_fd = -1,
    .gdbnotify_mutex = PTHREAD_MUTEX_INITIALIZER};
#define BUFMAX (16 * 1024)       // buffer for gdb protocol
static char in_buffer[BUFMAX];   // TODO: malloc/free these two
static unsigned char registers[BUFMAX];

#define GDB_ERROR_MSG "E01"   // The actual error code is ignored by GDB, so any number will do
static const char hexchars[] = "0123456789abcdef";

static void km_gdb_exit_debug_stopreply(km_vcpu_t* vcpu, char* stopreply);

static int hex(unsigned char ch)
{
   if ((ch >= 'a') && (ch <= 'f'))
      return (ch - 'a' + 10);
   if ((ch >= '0') && (ch <= '9'))
      return (ch - '0');
   if ((ch >= 'A') && (ch <= 'F'))
      return (ch - 'A' + 10);
   return -1;
}

/*
 * Converts the (count) bytes of memory pointed to by mem into an hex string in
 * buf. Returns a pointer to the last char put in buf (null).
 */
char* mem2hex(const unsigned char* mem, char* buf, size_t count)
{
   size_t i;
   unsigned char ch;

   for (i = 0; i < count; i++) {
      ch = *mem++;
      *buf++ = hexchars[ch >> 4];
      *buf++ = hexchars[ch % 16];
   }
   *buf = 0;
   return buf;
}

/*
 * Converts the hex string in buf into binary in mem.
 * Returns a pointer to the character AFTER the last byte written.
 */
static unsigned char* hex2mem(const char* buf, unsigned char* mem, size_t count)
{
   size_t i;
   unsigned char ch;

   assert(strlen(buf) >= (2 * count));

   for (i = 0; i < count; i++) {
      ch = hex(*buf++) << 4;
      ch = ch + hex(*buf++);
      *mem++ = ch;
   }
   return mem;
}

/*
 * Listens on (global) port and  when connection accepted/established, sets
 * global gdbstub.sock_fd and returns 0.
 * Returns -1 in case of failures.
 */
int km_gdb_wait_for_connect(const char* image_name)
{
   int listen_socket_fd;
   struct sockaddr_in server_addr, client_addr;
   struct in_addr ip_addr;
   socklen_t len;
   int opt = 1;

   assert(km_gdb_is_enabled() == 1);
   listen_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_socket_fd == -1) {
      warn("Could not create socket");
      return -1;
   }

   if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
      warn("setsockopt(SO_REUSEADDR) failed");
   }
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   server_addr.sin_port = htons(km_gdb_port_get());

   if (bind(listen_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
      warn("bind failed");
      return -1;
   }

   if (listen(listen_socket_fd, 1) == -1) {
      warn("listen failed");
      return -1;
   }

   warnx("Waiting for a debugger. Connect to it like this:");
   warnx("\tgdb --ex=\"target remote localhost:%d\" %s\nGdbServerStubStarted\n",
         km_gdb_port_get(),
         image_name);

   len = sizeof(client_addr);
   gdbstub.sock_fd = accept(listen_socket_fd, (struct sockaddr*)&client_addr, &len);
   if (gdbstub.sock_fd == -1) {
      warn("accept failed");
      return -1;
   }
   close(listen_socket_fd);

   if (setsockopt(gdbstub.sock_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
      warn("Setting TCP_NODELAY failed, continuing...");
   }
   ip_addr.s_addr = client_addr.sin_addr.s_addr;
   warnx("Connection from debugger at %s", inet_ntoa(ip_addr));
   return 0;
}

/*
 * Do whatever is need to ensure the vcpu is runnable.
 * Essentially all of gdb's tenticles should be removed from the vcpu.
 * Always returns 0.
 */
static int km_gdb_vcpu_disengage(km_vcpu_t* vcpu, uint64_t unused)
{
   vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_RUNNING;
   return 0;
}

/*
 * Initialize gdb's per vcpu state.
 * Called when a vcpu (a thread) comes to life.
 * You do want to called this when a parked vcpu is reused.
 */
void km_gdb_vcpu_state_init(km_vcpu_t* vcpu)
{
   vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_RUNNING;
}

/* closes the gdb socket and set port to 0 */
static void km_gdb_disable(void)
{
   if (km_gdb_is_enabled() != 1) {
      return;
   }

   // Disconnect gdb from all of the vcpu's
   km_vcpu_apply_all(km_gdb_vcpu_disengage, 0);

   km_gdb_port_set(0);
   warnx("Disabling gdb");
   if (gdbstub.sock_fd > 0) {
      close(gdbstub.sock_fd);
      gdbstub.sock_fd = -1;
   }
}

static int send_char(char ch)
{
   /* TCP is already buffering, so no need to buffer here as well. */
   return send(gdbstub.sock_fd, &ch, 1, 0);
}

/* get single char, blocking */
static char recv_char(void)
{
   unsigned char ch;
   int ret;

   while ((ret = recv(gdbstub.sock_fd, &ch, 1, 0)) < 0 && errno == EINTR) {
      ;   // ignore interrupts
   }
   if (ret <= 0) {
      km_gdb_disable();
      return -1;
   }
   /*
    * Paranoia check: at this poing we should get either printable '$...' command, or ^C, since we
    * do not support nor expect 'X' (binary data) packets.
    */
   assert(isprint(ch) || ch == GDB_INTERRUPT_PKT);
   return (char)ch;
}

/*
 * Scan for the sequence $<data>#<checksum>
 * Returns a null terminated string.
 */
static char* recv_packet(void)
{
   char* buffer = &in_buffer[0];
   unsigned char checksum;
   unsigned char xmitcsum;
   char ch;
   int count;

   while (1) {
      // wait around for the start character, ignore all other characters
      while ((ch = recv_char()) != '$') {
         if (ch == -1) {
            return NULL;
         }
      }

   retry:
      // Fill the buffer and calculate the checksum
      for (count = 0, checksum = 0; count < BUFMAX - 1; count++, checksum += ch) {
         if ((ch = recv_char()) == -1) {
            return NULL;
         }
         if (ch == '$') {
            goto retry;
         }
         if (ch == '#') {
            break;
         }
         buffer[count] = ch;
      }
      if (count == BUFMAX - 1) {
         warnx("gdb message too long, disconnecting");
         return NULL;
      }
      buffer[count] = '\0';   // Make this a C string.

      /* recv checksum from the packet  and compare with calculated one */
      assert(ch == '#');
      if ((ch = recv_char()) == -1) {
         return NULL;
      }
      xmitcsum = hex(ch) << 4;
      if ((ch = recv_char()) == -1) {
         return NULL;
      }
      xmitcsum += hex(ch);

      if (checksum != xmitcsum) {
         warnx("Failed checksum from GDB. "
               "Calculated= 0x%x, received=0x%x. buf=%s",
               checksum,
               xmitcsum,
               buffer);
         if (send_char('-') == -1) {
            // Unsuccessful reply to a failed checksum
            warnx("GDB: Could not send an ACK- to the debugger.");
            return NULL;
         }
      } else {
         km_infox(KM_TRACE_GDB, "%s: '%s', ack", __FUNCTION__, buffer);
         if (send_char('+') == -1) {
            // Unsuccessful reply to a successful transfer
            err(1, "GDB: Could not send an ACK+ to the debugger.");
            return NULL;
         }
         // if a sequence char is present, reply the sequence ID
         if (buffer[2] == ':') {
            send_char(buffer[0]);
            send_char(buffer[1]);
            return &buffer[3];
         }
         return &buffer[0];
      }
   }
}

/*
 * Send packet of the form $<packet info>#<checksum> without waiting for an ACK
 * from the debugger. Only send_response
 */
static void send_packet_no_ack(const char* buffer)
{
   unsigned char checksum;
   int count;
   char ch;

   /*
    * We ignore all send_char errors as it means that the connection
    * is broken so all sends for the packet will fail, and the next recv
    * for gdb ack will also fail take the necessary actions.
    */
   km_infox(KM_TRACE_GDB, "Sending packet '%s'", buffer);
   send_char('$');
   for (count = 0, checksum = 0; (ch = buffer[count]) != '\0'; count++, checksum += ch)
      ;
   send(gdbstub.sock_fd, buffer, count, 0);
   send_char('#');
   send_char(hexchars[checksum >> 4]);
   send_char(hexchars[checksum % 16]);
}

/*
 * Send a packet and wait for a successful ACK of '+' from the debugger.
 * An ACK of '-' means that we have to resend.
 */
static void send_packet(const char* buffer)
{
   for (char ch = '\0'; ch != '+';) {
      send_packet_no_ack(buffer);
      if ((ch = recv_char()) == -1) {
         return;
      }
   }
}

#define send_error_msg()                                                                           \
   do {                                                                                            \
      send_packet(GDB_ERROR_MSG);                                                                  \
   } while (0)
#define send_not_supported_msg()                                                                   \
   do {                                                                                            \
      send_packet("");                                                                             \
   } while (0)
#define send_okay_msg()                                                                            \
   do {                                                                                            \
      send_packet("OK");                                                                           \
   } while (0)

/*
 * This is a response to 'c', 's', and "vCont" gdb packets. In other words,
 * the VM was running and it stopped for some reason. This message is to tell the
 * debugger that we stopped (and why). The argument handling code can take these
 * and some other values:
 *    - 'S AA' received signal AA
 *    - 'W AA' process exited with return code AA
 *    - 'X AA' process exited with signal AA
 * We also handle sending stop replies ('T' and 'w') that contain thread id's if the gdb
 * client has told us it can handle this.
 * https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html
 * Parameters:
 *   signum - these should be gdb's signal numbers, like GDB_SIGNAL_ILL, as
 *      defined in the gdb source in include/gdb/signals.def
 *      In some cases the signals in /usr/include/asm/signal.h are the same
 *      so we can avoid xlating from linux signals into gdb signals.
 *      Also note that we overload the signal number with our own "signals"
 *      like thread create and thread exit since they act like signals in
 *      that threads are stopped when we notify the gdb client of these
 *      events.
 */
static void send_response(char code, gdb_event_t* gep, bool wait_for_ack)
{
   char obuf[BUFMAX];
   km_vcpu_t* vcpu;

   if (gdbstub.clientsup_vcontsupported && code == 'S') {
      // We can send stop replies that contain thread id's.
      // Since 'W' and 'X' don't contain thread id's let those fall into the else block.
      vcpu = km_vcpu_fetch_by_tid(gep->sigthreadid);
      switch (gep->signo) {
         case GDB_KMSIGNAL_KVMEXIT:
            // Use the exit reason to decide what stop reply to send.
            if (gep->exit_reason == KVM_EXIT_DEBUG) {
               km_gdb_exit_debug_stopreply(vcpu, obuf);
            } else {
               km_infox(KM_TRACE_GDB,
                        "%s: don't know how to handle exit_reason %d",
                        __FUNCTION__,
                        gep->exit_reason);
               abort();
            }
            break;
         case GDB_KMSIGNAL_THREADEXIT:
            snprintf(obuf, sizeof(obuf), "N");
            break;
         case SIGTRAP:
            snprintf(obuf, sizeof(obuf), "T%02Xswbreak:0;thread:%08X;", gep->signo, gep->sigthreadid);
            break;
         case SIGINT:
         case 0:
            snprintf(obuf, sizeof(obuf), "T%02Xthread:%08X;", gep->signo, gep->sigthreadid);
            break;
         default:
            // We need to supply additional registers here.
            snprintf(obuf, sizeof(obuf), "T%02Xthread:%08X;", gep->signo, gep->sigthreadid);
            break;
      }
   } else {
      snprintf(obuf, sizeof(obuf), "%c%02x", code, gep->signo);
   }
   if (wait_for_ack) {
      send_packet(obuf);
   } else {
      send_packet_no_ack(obuf);
   }
}

/*
 * Disect the "exception state" for a kvm debug exit to figure out what kind of stop packet
 * we should send back to the gdb client and then build that into stopreply.
 * This is where we come when hardware breakpoints and watchpoints fire.
 * I can't find any symbols for all of these hard coded numbers used below.
 * I got these numbers from the following intel document:
 *  https://software.intel.com/sites/default/files/managed/a4/60/325384-sdm-vol-3abcd.pdf
 * Look in chapter 17.
 */
static void km_gdb_exit_debug_stopreply(km_vcpu_t* vcpu, char* stopreply)
{
   struct kvm_debug_exit_arch* archp = &vcpu->cpu_run->debug.arch;
   void* addr;
   uint32_t type;

   /*
    * We have found that supplying register contents with the stop reply somehow causes
    * gdb stack traces to be incomplete.  So we leave the registers out of the stop
    * reply packes.  The gdb client is running the 'g' packet anyway so it is always
    * getting the registers we were supplying with the stop packet.
    */

   // Make sure we report good register contents.
   km_read_registers(vcpu);

   km_infox(KM_TRACE_VCPU,
            "%s: debug exception, vcpu %d, exception 0x%08x, pc 0x%016llX, dr6 0x%016llx, dr7 "
            "0x%016llx",
            __FUNCTION__,
            vcpu->vcpu_id,
            archp->exception,
            archp->pc,
            archp->dr6,
            archp->dr7);
   km_info(KM_TRACE_GDB,
           "sp %016llx, fp %016llx, pc %016llx",
           vcpu->regs.rsp,
           vcpu->regs.rbp,
           vcpu->regs.rip);

   if (archp->exception == 3) {
      // Apparently this is how a breakpoint (int 3) looks from KVM_EXIT_DEBUG.
      sprintf(stopreply, "T05thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   }

   /*
    * We have hit a hardware breakpoint but the client can't accept a hwbreak
    * stop reply.  So, we send the client a trap stop reply.
    * This is probably happening because a hardware single step was performed.
    */
   if (archp->exception == 1 && gdbstub.clientsup_hwbreak == 0) {
      assert((archp->dr6 & 0x4000) != 0);   // verify that this is a hw single step
      sprintf(stopreply, "T05thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   }

   /*
    * Ugly check to see which hw breakpoint fired.
    * The breakpoints we set in km_gdb_update_vcpu_debug()
    * are global but we check for both global and local here.
    */
   if ((archp->dr6 & 1) != 0 && (archp->dr7 & 0x03) != 0) {
      /* breakpoint in dr0 fired. */
      addr = (void*)vcpu->dr_regs[0];
      type = (archp->dr7 >> 16) & 0x03;
   } else if ((archp->dr6 & 2) != 0 && (archp->dr7 & 0x0c) != 0) {
      /* breakpoint in dr1 fired. */
      addr = (void*)vcpu->dr_regs[1];
      type = (archp->dr7 >> 20) & 0x03;
   } else if ((archp->dr6 & 4) != 0 && (archp->dr7 & 0x20) != 0) {
      /* breakpoint in dr2 fired */
      addr = (void*)vcpu->dr_regs[2];
      type = (archp->dr7 >> 24) & 0x03;
   } else if ((archp->dr6 & 8) != 0 && (archp->dr7 & 0xc0) != 0) {
      /* breakpoint in dr3 fired */
      addr = (void*)vcpu->dr_regs[3];
      type = (archp->dr7 >> 28) & 0x03;
   } else if ((archp->dr6 & 0x4000) != 0) {
      /* Single step exception. */
      /* Not sure if hwbreak is the right action for single step */
      sprintf(stopreply, "T05hwbreak:;thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   } else {
      km_infox(KM_TRACE_GDB,
               "%s: triggered hw breakpoint doesn't match the set hw breakpoints",
               __FUNCTION__);
      abort();
   }
   km_info(KM_TRACE_VCPU, "%s: addr %p, type 0x%x", __FUNCTION__, addr, type);

   /*
    * Convert the debug exception into the gdb stop packet type.
    * And, yes, we send the watch point address in big endian but the
    * register contents are sent in target native byte order.
    */
   switch (type) {
      case 0x00:   // break on instruction execution
         sprintf(stopreply, "T05hwbreak:;thread:%08x;", km_vcpu_get_tid(vcpu));
         break;
      case 0x01:   // break on data writes
         sprintf(stopreply,
                 "T05watch:%016llx;thread:%08x;",
                 (unsigned long long)addr,
                 km_vcpu_get_tid(vcpu));
         break;
      case 0x02:   // break on i/o reads or writes
         // Should we see this?  At this time I don't think so.
         abort();
         break;
      case 0x03:   // break on data reads or writes but not instruction fetches
         sprintf(stopreply,
                 "T05awatch:%016llx;thread:%08x;",
                 (unsigned long long)addr,
                 km_vcpu_get_tid(vcpu));
         break;
   }
}

static inline int km_gdb_thread_id(km_vcpu_t* vcpu)   // we use km_vcpu_get_tid as thread id for GDB
{
   return km_vcpu_get_tid(vcpu);
}

// Add hex gdb_tid to the list of thread_ids for communication to gdb
static int add_thread_id(km_vcpu_t* vcpu, uint64_t data)
{
   char* obuf = (char*)data;

   sprintf(obuf + strlen(obuf), "%x,", km_gdb_thread_id(vcpu));
   km_infox(KM_TRACE_GDB "threads", "add_thread_id: '%s'", obuf);
   return 0;
}

// Don't expect a kvm exit reason to be bigger than 64 bytes.
const int MAX_EXIT_REASON_STRING_SIZE = 64;

static void km_exit_reason_to_string(km_vcpu_t* vcpu, char* reason, size_t reason_size)
{
   reason[0] = 0;
   switch (vcpu->cpu_run->exit_reason) {
      case KVM_EXIT_DEBUG:
         snprintf(reason, reason_size, "%s", "Breakpoint");
         break;
      case KVM_EXIT_EXCEPTION:
         snprintf(reason, reason_size, "signal %d", km_signal_ready(vcpu));
         break;
      default:
         // Show nothing for other exit reasons.
         break;
   }
}

// Form and send thread list ('m<thread_ids>' packet) to gdb
static void send_threads_list(void)
{
   char obuf[BUFMAX] = "m";   // max thread is 288 (currently) so BUFMAX (16K) is more than enough

   km_vcpu_apply_all(add_thread_id, (uint64_t)obuf);
   obuf[strlen(obuf) - 1] = '\0';   // strip trailing comma
   send_packet(obuf);
}

/*
 * Description of the features the gdb client can claim it supports.
 */
typedef struct gdb_features {
   char* feature_name;   // from the qSupported packet
   bool feature_is_flag;
   uint8_t* feature_flag_or_value;
} gdb_features_t;

static gdb_features_t gdbclient_known_features[] = {
    {"multiprocess", true, &gdbstub.clientsup_multiprocess},
    {"xmlRegisters", true, &gdbstub.clientsup_xmlregisters},
    {"qRelocInsn", true, &gdbstub.clientsup_qRelocInsn},
    {"swbreak", true, &gdbstub.clientsup_swbreak},
    {"hwbreak", true, &gdbstub.clientsup_hwbreak},
    {"fork-events", true, &gdbstub.clientsup_forkevents},
    {"vfork-events", true, &gdbstub.clientsup_vforkevents},
    {"exec-events", true, &gdbstub.clientsup_execevents},
    {"vContSupported", true, &gdbstub.clientsup_vcontsupported},
    {"QThreadEvents", true, &gdbstub.clientsup_qthreadevents},
    {NULL, true, NULL},
};

/*
 * Process the "qSupported:xxx;yyy;zzz;..." packet from the gdb client.
 * We look at the options supplied in the packet and set the associated
 * flags in the gdbstub.  Then we supply the options supported by us
 * (the gdb server) in the reply to the gdb client.
 * This function sends whatever reply to the gdb client is required.
 */
static void handle_qsupported(char* packet, char* obuf)
{
   char* p = packet + strlen("qSupported");
   char* tokenp;
   char* savep;
   int i;

   *obuf = 0;

   /*
    * qSupported:
    * First parse what the gdb client is willing to do.
    * Then form a reply which tells the client what the gdb server is willing to do.
    */
   if (*p == ':') {
      /* Walk through the list of gdb client features and remember them for later */
      p++;
      while ((tokenp = strtok_r(p, ";", &savep)) != NULL) {
         p = NULL;
         for (i = 0; gdbclient_known_features[i].feature_name != NULL; i++) {
            size_t namelen;

            namelen = strlen(gdbclient_known_features[i].feature_name);
            if (strncmp(tokenp, gdbclient_known_features[i].feature_name, namelen) == 0) {
               switch (tokenp[namelen]) {
                  case '-':
                     *gdbclient_known_features[i].feature_flag_or_value = false;
                     break;
                  case '+':
                     *gdbclient_known_features[i].feature_flag_or_value = true;
                     break;
                  case '=':
                     *gdbclient_known_features[i].feature_flag_or_value = atoi(tokenp + namelen + 1);
                     break;
                  default:
                     /* protocol violation */
                     send_error_msg();
                     return;
               }
            } else {
               /* Unknown gdb client feature, we are supposed to ignore these */
            }
         } /* end for */
      }    /* end while */
   }
   /* Now tell the gdb client what the gdb server can do. */
   sprintf(obuf,
           "PacketSize=%08X;"
           "qXfer:threads:read+;"
           "swbreak+;"
           "hwbreak+;"
           "vContSupported+",
           BUFMAX - 1);
   send_packet(obuf);
}

/*
 * Storage area for a thread list in xml.
 * It will look like this:
 *  <?xml version="1.0"?>
 *  <threads>
 *    <thread id="id" core="0" name="name" handle="handle">
 *    ... description ...
 *    </thread>
 *  </threads>
 * 44 bytes for the outer xml wrapper
 * 42 bytes for each entry's xml wrapper and property names.
 * Assume:
 *  thread id   - max size 16 bytes.
 *  core        - max size 0, we don't have core information
 *  name        - max size 16, we don't have name information
 *  handle      - max size 0, we don't have handle information
 *  description - max size 0, we don't have description.
 * Update the sizes in the declaration below if we add values.
 * The thread list is delivered to the client in mutiple chunks, so
 * we must keep the entire list around until all of it has been
 * read by the gdb client.
 */
static char qxfer_thread_list[(KVM_MAX_VCPUS * (16 + 0 + 16 + 0 + 0 + 42)) + 44];
#define qxfer_thread_list_max_size (sizeof(qxfer_thread_list))

/*
 * Concatenate src onto the end of null terminated dest if there is
 * enough space to hold the result.  After copy, dest will still be
 * null terminated.
 *  destlen - the total amount of space in dest including what has already
 *    been copied in.
 * Returns:
 *  true - space was available and src was copied into dest.
 *  false - not enough space available and src was not copied.
 */
static bool stringcat(char* dest, char* src, int destlen)
{
   int curdestlen = strlen(dest);
   int cursrclen = strlen(src);

   if ((destlen - curdestlen) < (cursrclen + 1)) {
      return false;
   }
   memcpy(&dest[curdestlen], src, cursrclen);
   dest[curdestlen + cursrclen] = 0;

   return true;
}

/*
 * The max thread name we expect from pthread_getname_np().
 */
const int MAX_THREADNAME_SIZE = 32;
/*
 * The largest size we expect for an entry in the gdb thread list.
 * sizeof("  <thread id=\"%08x\" core=\"%08x\" name=\"%s\">\n  </thread>\n")
 */
const int MAX_THREADLISTENTRY_SIZE = 512;

static int build_thread_list_entry(km_vcpu_t* vcpu, uint64_t data)
{
   bool worked;
   char threadname[MAX_THREADNAME_SIZE];
   char threadlistentry[MAX_THREADLISTENTRY_SIZE];

   pthread_getname_np(vcpu->vcpu_thread, threadname, sizeof(threadname)); /* not really what we want here */

   snprintf(threadlistentry,
            sizeof(threadlistentry),
            "  <thread id=\"%08x\" core=\"%08x\" name=\"%s\">\n"
            "  </thread>\n",
            km_vcpu_get_tid(vcpu),
            vcpu->vcpu_id,
            threadname);
   worked = stringcat(qxfer_thread_list, threadlistentry, qxfer_thread_list_max_size);
   if (!worked) {
      return 1;
   }
   return 0;
}

/*
 * Parse the offset and length that come with this request.
 * This tells us where to resume and how much we can send if sending the
 * thread list in chunks.
 * Then generate an xml list of the threads into a chunk of allocated memory
 * or resume sending from a previously generated xml thread list.
 */
static void handle_qxfer_threads_read(char* packet, char* obuf)
{
   int offset;
   int length;
   bool worked;
   int n;
   int copythismuch;
   int total;
   int list_size;

   n = sscanf(packet, "qXfer:threads:read::%x,%x", &offset, &length);
   if (n == 2) {
      if (offset == 0) {
         qxfer_thread_list[0] = 0;

         /* Starting at the beginning so we need to generate the thread list header */
         worked = stringcat(qxfer_thread_list, "<threads>\n", sizeof(qxfer_thread_list));
         if (!worked) {
            /* Our space estimate is wrong, fix it! */
            send_error_msg();
            return;
         }
         /* Generate a thread entry for each thread */
         total = km_vcpu_apply_all(build_thread_list_entry, 0);
         if (total != 0) {
            /* The output string is full */
            send_error_msg();
            return;
         }
         /* Generate the thread list trailer */
         worked = stringcat(qxfer_thread_list, "</threads>\n", qxfer_thread_list_max_size);
         if (!worked) {
            /* space estimate is wrong, fix! */
            send_error_msg();
            return;
         }
      }
      /* Generate some or all of the reply from the saved buffer. */
      list_size = strlen(qxfer_thread_list);
      if (offset < list_size) {
         if (offset > list_size) {
            offset = list_size;
         }
         copythismuch = list_size - offset;
         if (copythismuch > length) {
            copythismuch = length;
         }
         obuf[0] = 'm';
         // TODO! We need to escape sensistive chars here.
         memcpy(&obuf[1], &qxfer_thread_list[offset], copythismuch);
         obuf[copythismuch + 1] = 0;
      } else {
         /* no more left to copy */
         obuf[0] = 'l';
         obuf[1] = 0;
      }
   } else {
      /* protocol violateion */
      send_error_msg();
      return;
   }
   send_packet(obuf);
}

/*
 * Given a thread id, offset and loaded module id, return the guest virtual
 * address it defines.
 * View this code as the plumbing to get us to the point of being able to handle
 * the qGetTLSAddr request from the gdb client.  The code that determines the
 * address returned is untested.
 */
static void handle_qgettlsaddr(char* packet, char* obuf)
{
   pid_t threadid;
#define tls_mod_off_t size_t   // should come from pthread_impl.h
   tls_mod_off_t offset;
   tls_mod_off_t lm;
   km_gva_t tcb_gva;
   km_kma_t tcb_kva;
   km_vcpu_t* vcpu;
   pthread_t* tcb;   // see struct pthread in pthread_impl.h
   tls_mod_off_t* dtv;
   km_gva_t tlsaddr;

   if (sscanf(packet, "qGetTLSAddr:%x,%lx,%lx", &threadid, &offset, &lm) == 3) {
      vcpu = km_vcpu_fetch_by_tid(threadid);
      if (vcpu != NULL) {
         // We model what we do after __tls_get_addr().  Since we are operating on behalf
         // of another thread we must do the computations ourselves.
         tcb_gva = vcpu->guest_thr;
         tcb_kva = km_gva_to_kma_nocheck(tcb_gva);
         tcb = tcb_kva;
#if 1
         dtv = (tls_mod_off_t*)tcb[1];
         tlsaddr = dtv[lm] + offset;
#else
         /* Use this if we include pthread_impl.h for the pthread definition. */
         tlsaddr = tcb->dtv[lm] + offset;
#endif
         sprintf(obuf, "%016lX", tlsaddr);
         send_packet(obuf);
      } else {
         // unknown thread id
         warnx("qGetTLSAddr: VCPU for thread %#x is not found", threadid);
         send_error_msg();
      }
   } else {
      // not enough args, protocol violation
      send_error_msg();
   }
}

// The guest tid we send back in the thread extra info is expected to be smaller than 64.
const int MAX_GUEST_TID_SIZE = 64;

// Handle general query packet ('q<query>'). Use obuf as output buffer.
static void km_gdb_general_query(char* packet, char* obuf)
{
   char exit_reason[MAX_EXIT_REASON_STRING_SIZE];

   if (strncmp(packet, "qfThreadInfo", strlen("qfThreadInfo")) == 0) {   // Get list of active
                                                                         // thread IDs
      send_threads_list();
   } else if (strncmp(packet, "qsThreadInfo", strlen("qsThreadInfo")) == 0) {   // Get more thread ids
      send_packet("l");   // 'l' means "no more thread ids"
   } else if (strncmp(packet, "qAttached", strlen("qAttached")) == 0) {
      send_packet("1");   // '1' means "the process was attached, not started"
   } else if (strncmp(packet, "qC", strlen("qC")) == 0) {   // Get the current thread_id
      char buf[64];

      sprintf(buf, "QC%x", km_gdb_thread_id(km_gdb_vcpu_get()));
      send_packet(buf);
   } else if (strncmp(packet, "qThreadExtraInfo", strlen("qThreadExtraInfo")) == 0) {   // Get label
      char label[MAX_GUEST_TID_SIZE + MAX_EXIT_REASON_STRING_SIZE];
      int thread_id;
      km_vcpu_t* vcpu;

      if (sscanf(packet, "qThreadExtraInfo,%x", &thread_id) != 1) {
         warnx("qThreadExtraInfo: wrong packet '%s'", packet);
         send_error_msg();
         return;
      }
      if ((vcpu = km_vcpu_fetch_by_tid(thread_id)) == NULL) {
         warnx("qThreadExtraInfo: VCPU for thread %#x is not found", thread_id);
         send_error_msg();
         return;
      }
      km_exit_reason_to_string(vcpu, exit_reason, sizeof(exit_reason));
      snprintf(label,
               sizeof(label),
               "Guest 0x%lx, %s",   // guest pthread pointer in free form label and reason for stopping
               vcpu->guest_thr,
               exit_reason);
      mem2hex((unsigned char*)label, obuf, strlen(label));
      send_packet(obuf);
   } else if (strncmp(packet, "qSupported", strlen("qSupported")) == 0) {
      handle_qsupported(packet, obuf);
   } else if (strncmp(packet, "qXfer:threads:read:", strlen("qXfer:threads:read:")) == 0) {
      handle_qxfer_threads_read(packet, obuf);
   } else if (strncmp(packet, "qGetTLSAddr:", strlen("qGetTLSAddr:")) == 0) {
      handle_qgettlsaddr(packet, obuf);
   } else {
      send_not_supported_msg();
   }
}

/*
 * When we get a vCont command each thread's run state can be set.
 * We remember what was requested and apply these values after the entire
 * command is validated.
 */
struct threadaction {
   gdb_run_state_t ta_newrunstate;
   km_gva_t ta_steprange_start;   // if ta_newrunstate is GRS_RANGESTEPPING, the beginning of the range
   km_gva_t ta_steprange_end;   // the end of the range stepping address range
};
typedef struct threadaction threadaction_t;

struct threadaction_blob {
   threadaction_t threadaction[KVM_MAX_VCPUS];   // each thread is bound to a virtual cpu
};
typedef struct threadaction_blob threadaction_blob_t;

/*
 * Apply the vCont action to the vcpu for each thread.
 * Since payload threads are vcpus, operating on a cpu is operating
 * on the thread.
 */
static int km_gdb_set_thread_vcont_actions(km_vcpu_t* vcpu, uint64_t ta)
{
   int i;
   int rc;
   threadaction_blob_t* threadactionblob = (threadaction_blob_t*)ta;

   i = vcpu->vcpu_id;
   switch (threadactionblob->threadaction[i].ta_newrunstate) {
      case GRS_NONE:
         rc = 0;
         vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_PAUSED;
         break;
      case GRS_RUNNING:
         vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_RUNNING;
         gdbstub.stepping = false;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      case GRS_RANGESTEPPING:
         vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_RANGESTEPPING;
         vcpu->gdb_vcpu_state.gvs_steprange_start =
             threadactionblob->threadaction[i].ta_steprange_start;
         vcpu->gdb_vcpu_state.gvs_steprange_end = threadactionblob->threadaction[i].ta_steprange_end;
         gdbstub.stepping = true;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      case GRS_STEPPING:
         vcpu->gdb_vcpu_state.gvs_gdb_run_state = GRS_STEPPING;
         gdbstub.stepping = true;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      default:
         rc = -1;
         km_infox(KM_TRACE_GDB,
                  "%s: vcpu %d, unhandled vcpu run state %d",
                  __FUNCTION__,
                  i,
                  threadactionblob->threadaction[i].ta_newrunstate);
         break;
   }
   km_infox(KM_TRACE_GDB,
            "%s: vcpu %d: is_paused %d, ta_newrunstate %d, gvs_gdb_run_state %d",
            __FUNCTION__,
            i,
            vcpu->is_paused,
            threadactionblob->threadaction[i].ta_newrunstate,
            vcpu->gdb_vcpu_state.gvs_gdb_run_state);
   return rc;
}

static int linux_signo(gdb_signal_number_t);

/*
 * We handle the vCont;action:tid:tid:tid:...][;action:tid:tid:...]... command
 * here. If a thread is mentioned in more that one action the leftmost action
 * takes precedence.
 * We setup the thread's debug registers for each mentioned thread.
 * If a thread is unmentioned, its debug state is set to continue.
 * If an action has no tid list, then the action applies to all threads.
 * Parameteres:
 *   packet - the request from the gdb client
 *   obuf - a buffer to build the command reply into
 *   resume - returns a flag that indicates the target is to continue executing or
 *     is to remain in the gdb server waiting for more commands from the gdb
 *     client.
 *     true - means the target is to go back to running.
 *     false - the gdb server code waits for more commands from the gdb client
 */
static void km_gdb_handle_vcontpacket(char* packet, char* obuf, int* resume)
{
   char* p;
   char* tokenp;
   char* savep;
   char* p1;
   char* tokenp1;
   char* savep1;
   threadaction_blob_t threadactionblob;
   unsigned int tid;
   char* endp;
   int i;
   km_vcpu_t* vcpu;
   int count;
   int gdbsigno = 0;
   int linuxsigno = 0;
   km_gva_t startaddr;
   km_gva_t endaddr;

   /*
    * Initialize the per thread(vcpu) actions for vCont.
    */
   for (i = 0; i < KVM_MAX_VCPUS; i++) {
      threadactionblob.threadaction[i].ta_newrunstate = GRS_NONE;
   }

   if (packet[5] == ';') {
      /* Examine each action, tidlist group */
      p = &packet[6];
      while ((tokenp = strtok_r(p, ";", &savep)) != NULL) {
         char cmd;

         gdbsigno = 0;
         linuxsigno = 0;
         p = NULL;
         cmd = tokenp[0];
         switch (cmd) {
            case 'r':
               /*
                * Handle stepping through a range of addresses.
                *  vCont;rSSSSS,EEEEE:threadid
                * We only support a single thread id but the command syntax
                * implies there could be multiple threads step through the same range.
                * The stepping is handled in km_vcpu_run().
                */
               if (sscanf(&tokenp[1], "%lx,%lx:%x", &startaddr, &endaddr, &tid) == 3) {
                  if (startaddr > endaddr) {
                     send_error_msg();
                     return;
                  }
                  vcpu = km_vcpu_fetch_by_tid(tid);
                  if (vcpu == NULL) {
                     /* invalid tid should we ignore or fail? */
                     km_info(KM_TRACE_GDB, "%s: tid %d is unknown?", __FUNCTION__, tid);
                     send_error_msg();
                     return;
                  } else {
                     /* Use the id of the thread's vcpu as our index */
                     i = vcpu->vcpu_id;
                     if (threadactionblob.threadaction[i].ta_newrunstate == GRS_NONE) {
                        threadactionblob.threadaction[i].ta_newrunstate = GRS_RANGESTEPPING;
                        threadactionblob.threadaction[i].ta_steprange_start = startaddr;
                        threadactionblob.threadaction[i].ta_steprange_end = endaddr;
                     } else {
                        // An earlier action already specified what to do for this thread
                     }
                  }
               } else {
                  // Couldn't scan the args to step range.
                  send_error_msg();
                  return;
               }
               break;
            case 'C':
            case 'S':
               // Parse the signal number.
               // We expect something like this: Css:tttt or Sss:tttt
               // Where ss is a signal number and tttt is a thread id
               if (sscanf(&tokenp[1], "%02x", &gdbsigno) == 1) {
                  // Validate signal number
                  if (gdbsigno > 0 && gdbsigno <= 44) {
                     // Valid. Xlate the gdb signal number to the linux signal number
                     linuxsigno = linux_signo(gdbsigno);
                  } else {
                     send_error_msg();
                     return;
                  }
               } else {
                  // Missing signal number?
                  send_error_msg();
                  return;
               }
               tokenp += 2;   // push past the signal number (sort of)
            case 'c':
            case 's':
               if (tokenp[1] == ':') {
                  /* We have a list of tids, parse them */
                  p1 = &tokenp[2];
                  while ((tokenp1 = strtok_r(p1, ":", &savep1)) != NULL) {
                     p1 = NULL;
                     if (*tokenp1 == 0) {
                        /* A ':' but nothing after it? */
                        send_error_msg();
                        return;
                     }
                     tid = strtoul(tokenp1, &endp, 16);
                     if (*tokenp1 != 0 && *endp == 0) {
                        /*
                         * We have a successful conversion.
                         * Map tid to an index into threadaction[].
                         * For now, the vcpu_id is an array index.  So,
                         * we use that for our index.
                         */
                        vcpu = km_vcpu_fetch_by_tid(tid);
                        if (vcpu == NULL) {
                           /* invalid tid should we ignore or fail? */
                           km_info(KM_TRACE_GDB, "%s: tid %d is unknown?", __FUNCTION__, tid);
                           send_error_msg();
                           return;
                        } else {
                           /* Use the id of the thread's cpu as our index */
                           i = vcpu->vcpu_id;
                           if (threadactionblob.threadaction[i].ta_newrunstate == GRS_NONE) {
                              if (cmd == 'c' || cmd == 'C') {
                                 threadactionblob.threadaction[i].ta_newrunstate = GRS_RUNNING;
                              } else {
                                 threadactionblob.threadaction[i].ta_newrunstate = GRS_STEPPING;
                              }
                           } else {
                              /* An earlier action already specified what to do for this thread */
                           }
                           // If a signal is to be sent, enqueue that now.
                           if (cmd == 'C' || cmd == 'S') {
                              siginfo_t info;

                              km_infox(KM_TRACE_GDB,
                                       "%s: post linux signal %d (gdb %d) to thread %d",
                                       __FUNCTION__,
                                       linuxsigno,
                                       gdbsigno,
                                       tid);
                              info.si_signo = linuxsigno;
                              info.si_code = SI_USER;
                              km_post_signal(vcpu, &info);
                           }
                        }
                     } else {
                        /* Not a valid number */
                        km_info(KM_TRACE_GDB, "%s: %s is not a valid number?", __FUNCTION__, tokenp1);
                        send_error_msg();
                        return;
                     }
                  } /* end of while tid list */
               } else if (tokenp[1] == 0) {
                  /* No tid list, the action applies to all threads with no action yet */
                  for (i = 0; i < KVM_MAX_VCPUS; i++) {
                     if (threadactionblob.threadaction[i].ta_newrunstate == GRS_NONE) {
                        if (tokenp[0] == 'c' || tokenp[0] == 'C') {
                           threadactionblob.threadaction[i].ta_newrunstate = GRS_RUNNING;
                        } else {
                           threadactionblob.threadaction[i].ta_newrunstate = GRS_STEPPING;
                        }
                     }
                  }
               } else {
                  // Some other trash after the action
                  km_infox(KM_TRACE_GDB, "%s: unexpected stuff '%s' after action", __FUNCTION__, tokenp);
                  send_error_msg();
                  return;
               }
               break;
            default:
               /* We don't support thread stop yet */
               km_info(KM_TRACE_GDB, "%s: unsupported continue option '%s'", __FUNCTION__, tokenp);
               send_not_supported_msg();
               return;
         }
      }
   } else {
      /* There must be an action. */
      km_info(KM_TRACE_GDB, "%s: missing semicolon in %s'", __FUNCTION__, packet);
      send_error_msg();
      return;
   }

   /*
    * We made it through the vCont arguments, now apply what we were
    * asked to do.
    * Since km threads are each a vcpu, we just traverse the vcpus to
    * have each thread's vCont actions applied.
    */
   count = km_vcpu_apply_all(km_gdb_set_thread_vcont_actions, (uint64_t)&threadactionblob);
   if (count != 0) {
      km_info(KM_TRACE_GDB, "%s: apply all vcpus failed, count %d", __FUNCTION__, count);
      send_error_msg();
   } else {
      *resume = true;
   }
   return;
}

/*
 * The general v packet handler.
 * We currently handle:
 *  vCont? - gdb client finding out if we support vCont.
 *  vCont - gdb client tells us to continue or step instruction
 */
static void km_gdb_handle_vpackets(char* packet, char* obuf, int* resume)
{
   *resume = 0;
   if (strncmp(packet, "vCont?", strlen("vCont?")) == 0) {
      /*
       * Tell the gdb client what vCont options we support.
       * gdb will only support "c" if "C" is also supported.  Strange.
       */
      gdbstub.clientsup_vcontsupported = 1;
      strcpy(obuf, "vCont;c;s;C;S;r");
      send_packet(obuf);
   } else if (strncmp(packet, "vCont", strlen("vCont")) == 0) {
      /* gdb client wants us to run the app. */
      km_gdb_handle_vcontpacket(packet, obuf, resume);
   } else {
      /* We don't support this. */
      send_not_supported_msg();
   }
}

/*
 * Handle KVM_RUN exit.
 * Conduct dialog with gdb, until gdb orders next run (e.g. "next"), at which points return
 * control so the payload may continue to run.
 *
 * Note: Before calling this function, KVM exit_reason is converted to signum.
 * TODO: split this function into a sane table-driven set of handlers based on parsed command.
 */
static void gdb_handle_payload_stop(gdb_event_t* gep)
{
   km_vcpu_t* vcpu = NULL;
   char* packet;
   char obuf[BUFMAX];
   int signo;

   km_infox(KM_TRACE_GDB,
            "%s: signum %d, sigthreadid %d, exit_reason %d",
            __FUNCTION__,
            gep->signo,
            gep->sigthreadid,
            gep->exit_reason);
   if (gep->signo != GDB_SIGFIRST) {   // Notify the debugger about our last signal
      send_response('S', gep, true);

      // Switch to the cpu the signal happened on
      if (gep->sigthreadid > 0) {
         vcpu = km_vcpu_fetch_by_tid(gep->sigthreadid);
         km_gdb_vcpu_set(vcpu);
      }
   }
   while ((packet = recv_packet()) != NULL) {
      km_gva_t addr = 0;
      km_kma_t kma;
      gdb_breakpoint_type_t type;
      size_t len;
      int command, ret;

      km_infox(KM_TRACE_GDB, "Got packet: '%s'", packet);
      command = packet[0];
      switch (command) {
         case '!': {   // allow extended-remote mode. TODO: we don't support it fully, maybe decline?
            send_okay_msg();
            break;
         }
         case 'H': {   // Set thread for subsequent operations.
            /*
             * ‘H op thread-id’. op should be ‘c’ for step and continue  and ‘g’
             * for other operations.
             * TODO: this is deprecated, supporting the ‘vCont’ command is a better option.
             * See https://sourceware.org/gdb/onlinedocs/gdb/Packets.html#Packets)
             *
             * If requested, change VCPU we are working on.
             * thread_id == -1 means "all threads". 0 means "any thread". "-1" is really
             * used for 'non stop" mode only, which we don't support, so for both cases we use main vcpu
             */
            int thread_id;
            char cmd;
            vcpu = km_main_vcpu();   // TODO: use 1st vcpu in_use here !

            if (sscanf(packet, "H%c%x", &cmd, &thread_id) != 2 || (cmd != 'g' && cmd != 'c')) {
               warnx("Wrong 'H' packet '%s'", packet);
               send_error_msg();
               break;
            }
            if (thread_id > 0 && (vcpu = km_vcpu_fetch_by_tid(thread_id)) == NULL) {
               warnx("Can't find vcpu for tid %d (%#x) ", thread_id, thread_id);
               send_error_msg();
               break;
            }
            km_gdb_vcpu_set(vcpu);   // memorize it for future sessions ??
            send_okay_msg();
            break;
         }
         case 'T': {   // ‘T thread-id’.   Find out if the thread thread-id is alive.
            int thread_id;

            if (sscanf(packet, "T%x", &thread_id) != 1 || km_vcpu_fetch_by_tid(thread_id) == NULL) {
               km_infox(KM_TRACE_GDB "threads", "Reporting thread for vcpu %d as dead", thread_id);
               send_error_msg();
               break;
            }
            send_okay_msg();
            break;
         }
         case 'S': {
            if (sscanf(packet, "C%02x", &signo) == 1) {
               siginfo_t info;

               // We should also check for the optional but unsupported addr argument.

               // Prevent out of range gdb signal numbers
               if (signo < 0 || signo > 44) {
                  send_error_msg();
                  break;
               }

               info.si_signo = linux_signo(signo);
               info.si_code = SI_USER;
               km_post_signal(vcpu, &info);
            } else {
               // The signal number is bad or missing
               send_error_msg();
               break;
            }
            if (km_gdb_enable_ss() == -1) {
               send_error_msg();
               break;
            }
            goto done;   // Continue with program
         }
         case 's': {   // Step
            if (sscanf(packet, "s%" PRIx64, &addr) == 1) {
               /* not supported, but that's OK as GDB will retry with the
                * slower version of this: update all registers. */
               send_not_supported_msg();
               break;
            }
#if 0
            if (km_gdb_enable_ss() == -1) {
               send_error_msg();
               break;
            }
#else
            // We want to single step the current thread only.
            // So, disable single stepping on all then enable single step on the current cpu.
            if (km_gdb_disable_ss() == -1) {
               send_error_msg();
               break;
            }
            gdbstub.stepping = true;
            vcpu = km_gdb_vcpu_get();
            if (km_gdb_update_vcpu_debug(vcpu, 0) != 0) {
               send_error_msg();
               break;
            }
#endif
            goto done;   // Continue with program
         }
         case 'C': {
            if (sscanf(packet, "C%02x", &signo) == 1) {
               siginfo_t info;

               // We should also check for the optional but unsupported addr argument.

               // Prevent out of range gdb signal numbers
               if (signo < 0 || signo > 44) {
                  send_error_msg();
                  break;
               }

               info.si_signo = linux_signo(signo);
               info.si_code = SI_USER;
               km_post_signal(vcpu, &info);
            } else {
               // The signal number is bad or missing
               send_error_msg();
               break;
            }
            // Turn off single step
            if (km_gdb_disable_ss() == -1) {
               send_error_msg();
               break;
            }
            goto done;   // Continue with program
         }
         case 'c': {   // Continue (and disable stepping for the next instruction)
            if (sscanf(packet, "c%" PRIx64, &addr) == 1) {
               /* not supported, but that's OK as GDB will retry with the
                * slower version of this: update all registers. */
               send_not_supported_msg();
               break;
            }
            if (km_gdb_disable_ss() == -1) {
               send_error_msg();
               break;
            }
            goto done;   // Continue with program
         }
         case 'm': {   // Read memory content
            if (sscanf(packet, "m%" PRIx64 ",%zx", &addr, &len) != 2) {
               send_error_msg();
               break;
            }
            if ((kma = km_gva_to_kma(addr)) == NULL) {
               send_error_msg();
               break;
            }
            km_guest_mem2hex(addr, kma, obuf, len);
            send_packet(obuf);
            break;
         }
         case 'M': {   // Write memory content
            assert(strlen(packet) <= sizeof(obuf));
            if (sscanf(packet, "M%" PRIx64 ",%zx:%s", &addr, &len, obuf) != 3) {
               send_error_msg();
               break;
            }
            if ((kma = km_gva_to_kma(addr)) == NULL) {
               send_error_msg();
               break;
            }
            hex2mem(obuf, kma, len);
            send_okay_msg();
            break;
         }
         case 'g': {   // Read general registers
            km_vcpu_t* vcpu = km_gdb_vcpu_get();

            km_infox(KM_TRACE_GDB, "%s: reporting registers for vcpu %d", __FUNCTION__, vcpu->vcpu_id);
            len = BUFMAX;
            if (km_gdb_read_registers(vcpu, registers, &len) == -1) {
               send_error_msg();
               break;
            }
            mem2hex(registers, obuf, len);
            send_packet(obuf);
            break;
         }
         case 'G': {   // Write general registers
            km_vcpu_t* vcpu = km_gdb_vcpu_get();

            len = BUFMAX;
            /* Call read_registers just to get len (not very efficient). */
            if (km_gdb_read_registers(vcpu, registers, &len) == -1) {
               send_error_msg();
               break;
            }
            /* Packet looks like 'Gxxxxx', so we have to skip the first char */
            hex2mem(packet + 1, registers, len);
            if (km_gdb_write_registers(vcpu, registers, len) == -1) {
               send_error_msg();
               break;
            }
            send_okay_msg();
            break;
         }
         case '?': {   //  Return last signal
            send_response('S', gep, true);
            break;
         }
         case 'Z':     // Insert a breakpoint
         case 'z': {   // Remove a breakpoint
            packet++;
            if (sscanf(packet, "%" PRIx32 ",%" PRIx64 ",%zx", &type, &addr, &len) != 3) {
               send_error_msg();
               break;
            }
            if ((kma = km_gva_to_kma(addr)) == NULL) {
               send_error_msg();
               break;
            }
            if (command == 'Z') {
               ret = km_gdb_add_breakpoint(type, addr, len);
            } else {
               ret = km_gdb_remove_breakpoint(type, addr, len);
            }
            if (ret == -1) {
               send_error_msg();
            } else {
               send_okay_msg();
            }
            break;
         }
         case 'k': {   // quit ('kill the inferior')
            warnx("Debugger asked us to quit");
            send_okay_msg();
            errx(1, "Quiting per debugger request");
            goto done;   // not reachable
         }
         case 'D': {   // Detach
            warnx("Debugger detached");
            send_okay_msg();
            km_gdb_disable();
            goto done;
         }
         case 'q': {   // General query
            km_gdb_general_query(packet, obuf);
            break;
         }
         case 'v': {
            int resume;

            km_gdb_handle_vpackets(packet, obuf, &resume);
            if (resume) {
               /* A vCont command has started the payload running */
               goto done;
            } else {
               // A response has been sent, wait for another command.
            }
            break;
         }
         default: {
            send_not_supported_msg();
            break;
         }
      }   // switch
   }      // while
done:
   return;
}

/*
 * This function is called on GDB thread to give gdb a chance to handle the KVM exit.
 * It is expected to be called ONLY when gdb is enabled and ONLY when when all vCPUs are paused.
 * If the exit reason is of interest to gdb, the function lets gdb to handle it. If the exit
 * reason is of no interest to gdb, the function simply returns and lets vcpu loop to handle the
 * rest as it sees fit.
 *
 * Note that we map VM exit reason to a GDB 'signal', which is what needs to be communicated back
 * to gdb client.
 */
static void km_gdb_handle_kvm_exit(int is_intr, gdb_event_t* gep)
{
   assert(km_gdb_is_enabled() == 1);
   if (is_intr) {
      /*
       * gdb client interrupted the gdb server.  There is really no current thread
       * in this case but we need to be careful that the thread gdb thinks is
       * the current default thread still exists.  For consistency we just make
       * the main thread the current thread.
       */
      gep->signo = SIGINT;
      gep->sigthreadid = km_vcpu_get_tid(km_main_vcpu());
      gdb_handle_payload_stop(gep);
      return;
   }

   switch (gep->exit_reason) {
      case KVM_EXIT_DEBUG:
         gdb_handle_payload_stop(gep);
         return;

      /*
       * We can get here because of the SIGUSR1 used to stop all the vcpu's
       * when the gdb client stops the target.  We can also get here when the
       * target faults.
       */
      case KVM_EXIT_INTR:
         gdb_handle_payload_stop(gep);
         return;

      case KVM_EXIT_EXCEPTION:
         gep->signo = SIGSEGV;
         gdb_handle_payload_stop(gep);
         return;

      default:
         warnx("%s: Unknown reason %d, ignoring.", __FUNCTION__, gep->exit_reason);
         return;
   }
   return;
}

// Read and discard pending eventfd reads, if any. Non-blocking.
static void km_empty_out_eventfd(int fd)
{
   int flags = fcntl(fd, F_GETFL);
   fcntl(fd, F_SETFL, flags | O_NONBLOCK);
   km_wait_on_eventfd(fd);
   fcntl(fd, F_SETFL, flags);
}

/*
 * Unblock the passed vcpu.
 * If another vcpu has hit a breakpoint causing session_requested to be non-zero,
 * then don't start up this vcpu.  This is to avoid starting the remaining vcpu's
 * when a freshly started vcpu runs into a new breakpoint.
 * returns 0 on success and 1 on failure. Failures just counted by upstairs for reporting
 */
static inline int km_gdb_vcpu_continue(km_vcpu_t* vcpu, __attribute__((unused)) uint64_t unused)
{
   int ret = 1;

   if (vcpu->gdb_vcpu_state.gvs_gdb_run_state == GRS_PAUSED) {
      // gdb wants this thread paused so don't wake it up.
      ret = 0;
   } else {
      km_infox(KM_TRACE_GDB,
               "%s: waking up vcpu %d, gdb_run_state %d",
               __FUNCTION__,
               vcpu->vcpu_id,
               vcpu->gdb_vcpu_state.gvs_gdb_run_state);
      if (gdbstub.session_requested == 0 && (ret = km_gdb_cv_signal(vcpu) != 0)) {
         ;
      }
   }
   return ret == 0 ? 0 : 1;   // Unblock main guest thread
}

/*
 * Loop on waiting on either ^C from gdb client, or a vcpu exit from kvm_run for a reason relevant
 * to gdb. When a wait is over (for either of the reasons), stops all vcpus and lets GDB handle the
 * exit. When gdb says "next" or "continue" or "step", signals vcpu to continue and reenters the loop.
 */
void km_gdb_main_loop(km_vcpu_t* main_vcpu)
{
   struct pollfd fds[] = {
       {.fd = gdbstub.sock_fd, .events = POLLIN | POLLERR},
       {.fd = machine.intr_fd, .events = POLLIN | POLLERR},
   };
   gdb_event_t ge;

   km_wait_on_eventfd(machine.intr_fd);   // Wait for km_vcpu_run_main to set vcpu->tid
   km_gdb_vcpu_set(main_vcpu);
   ge.signo = GDB_SIGFIRST;
   ge.sigthreadid = km_vcpu_get_tid(main_vcpu);
   gdb_handle_payload_stop(&ge);   // Talk to GDB first time, before any vCPU run
   km_gdb_vcpu_continue(main_vcpu, 0);
   while (km_gdb_is_enabled() == 1) {
      int ret;
      int is_intr;

      // Poll two fds described above in fds[], with no timeout ("-1")
      while ((ret = poll(fds, 2, -1) == -1) && (errno == EAGAIN || errno == EINTR)) {
         ;   // ignore signals which may interrupt the poll
      }
      if (ret < 0) {
         err(1, "%s: poll failed ret=%d.", __FUNCTION__, ret);
      }
      if (machine.vm_vcpu_run_cnt == 0) {
         ge.signo = ret;
         send_response('W', &ge, true);   // inferior normal exit
         km_gdb_disable();
         return;
      }
      machine.pause_requested = 1;
      km_infox(KM_TRACE_GDB, "%s: Signalling vCPUs to pause", __FUNCTION__);
      km_vcpu_apply_all(km_vcpu_pause, 0);
      km_vcpu_wait_for_all_to_pause();
      km_infox(KM_TRACE_GDB, "%s: vCPUs paused. run_cnt %d", __FUNCTION__, machine.vm_vcpu_run_cnt);
      is_intr = 0;
      if (fds[0].revents) {   // got something from gdb client (hopefully ^C)
         int ch = recv_char();
         km_infox(KM_TRACE_GDB, "%s: got a msg from a client. ch=%d", __FUNCTION__, ch);
         if (ch == -1) {   // channel error or EOF (ch == -1)
            break;
         }
         assert(ch == GDB_INTERRUPT_PKT);   // At this point it's only legal to see ^C from GDB
         ret = pthread_mutex_lock(&gdbstub.gdbnotify_mutex);
         assert(ret == 0);
         /*
          * If a payload thread has already stopped it will have caused session_requested
          * to be set non-zero.  In this case we prefer to use the stopped thread as
          * opposed to the user ^C as the reason for breaking to gdb command level.
          */
         if (gdbstub.session_requested == 0) {
            gdbstub.session_requested = 1;
            is_intr = 1;
         }
         ret = pthread_mutex_unlock(&gdbstub.gdbnotify_mutex);
         assert(ret == 0);
      }
      if (fds[1].revents) {
         km_infox(KM_TRACE_GDB, "%s: a vcpu signalled about a kvm exit", __FUNCTION__);

         /*
          * Harvest a consistent description of what event happened
          * This avoids problems where we report back to the gdb client that
          * a breakpoint has happened then another event comes in and sets
          * a different thread id.  Then we set the vcpu using that thread id.
          * Then the gdb client queries for the registers and discovers a
          * breakpoint where it doesn't think there should be one.
          */
         ret = pthread_mutex_lock(&gdbstub.gdbnotify_mutex);
         assert(ret == 0);
         ge.signo = gdbstub.signo;
         ge.sigthreadid = gdbstub.sigthreadid;
         ge.exit_reason = gdbstub.exit_reason;
         ret = pthread_mutex_unlock(&gdbstub.gdbnotify_mutex);
         assert(ret == 0);
      }
      km_empty_out_eventfd(machine.intr_fd);   // discard extra 'intr' events if vcpus sent them
      km_gdb_handle_kvm_exit(is_intr, &ge);    // give control back to gdb

      km_infox(KM_TRACE_GDB, "%s: kvm exit handled, starting vcpu's", __FUNCTION__);

      // Allow the started vcpu's to wakeup this thread now.
      gdbstub.signo = 0;
      gdbstub.sigthreadid = 0;
      gdbstub.session_requested = 0;
      machine.pause_requested = 0;
      km_vcpu_apply_all(km_gdb_vcpu_continue, 0);
   }
}

/*
 * GDB uses it's own notion of signal number which is different that the native signal
 * number. See https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html.
 * GDB's signal numbers are the 'Sparc and Alpha' numbers defined in 'man 7 signal'.
 *
 * This function translates a X86 signal number to it's equivilent GDB signal number.
 * The hardcoded numbers below are from the file include/gdb/signals.def in a gdb
 * source tarball.
 */
static inline gdb_signal_number_t gdb_signo(int linuxsig)
{
   gdb_signal_number_t linuxsig2gdbsig[] = {
       0,
       GDB_SIGNAL_HUP,      // SIGHUP    1
       GDB_SIGNAL_INT,      // SIGINT    2
       GDB_SIGNAL_QUIT,     // SIGQUIT   3
       GDB_SIGNAL_ILL,      // SIGILL    4
       GDB_SIGNAL_TRAP,     // SIGTRAP   5
       GDB_SIGNAL_ABRT,     // SIGABRT   6
       GDB_SIGNAL_BUS,      // SIGBUS    7
       GDB_SIGNAL_FPE,      // SIGFPE    8
       GDB_SIGNAL_KILL,     // SIGKILL   9
       GDB_SIGNAL_USR1,     // SIGUSR1   10
       GDB_SIGNAL_SEGV,     // SIGSEGV   11
       GDB_SIGNAL_USR2,     // SIGUSR2   12
       GDB_SIGNAL_PIPE,     // SIGPIPE   13
       GDB_SIGNAL_ALRM,     // SIGALRM   14
       GDB_SIGNAL_TERM,     // SIGTERM   15
       0,                   // SIGSTKFLT 16
       GDB_SIGNAL_CHLD,     // SIGCHLD   17
       GDB_SIGNAL_CONT,     // SIGCONT   18
       GDB_SIGNAL_STOP,     // SIGSTOP   19
       GDB_SIGNAL_TSTP,     // SIGTSTP   20
       GDB_SIGNAL_TTIN,     // SIGTTIN   21
       GDB_SIGNAL_TTOU,     // SIGTTOU   22
       GDB_SIGNAL_URG,      // SIGURG    23
       GDB_SIGNAL_XCPU,     // SIGXCPU   24
       GDB_SIGNAL_XFSZ,     // SIGXFSZ   25
       GDB_SIGNAL_VTALRM,   // SIGVTALRM 26
       GDB_SIGNAL_PROF,     // SIGPROF   27
       GDB_SIGNAL_WINCH,    // SIGWINCH  28
       GDB_SIGNAL_IO,       // SIGIO     29
       GDB_SIGNAL_POLL,     // SIGPOLL   29
       GDB_SIGNAL_PWR,      // SIGPWR    30
       GDB_SIGNAL_SYS,      // SIGSYS    31
   };

   if (linuxsig > SIGSYS || linuxsig2gdbsig[linuxsig] == 0) {
      // out of range, just give them what they passed in
      km_infox(KM_TRACE_GDB, "No gdb signal for linux signal %d", linuxsig);
      return (gdb_signal_number_t)linuxsig;
   }
   return linuxsig2gdbsig[linuxsig];
}

/*
 * Given a gdb signal number convert it to a linux signal number.
 */
static int linux_signo(gdb_signal_number_t gdb_signo)
{
   int gdbsig2linuxsig[] = {
       0,
       SIGHUP,      // GDB_SIGNAL_HUP  1
       SIGINT,      // GDB_SIGNAL_INT  2
       SIGQUIT,     // GDB_SIGNAL_QUIT 3
       SIGILL,      // GDB_SIGNAL_ILL  4
       SIGTRAP,     // GDB_SIGNAL_TRAP 5
       SIGABRT,     // GDB_SIGNAL_ABRT 6
       0,           // GDB_SIGNAL_EMT  7
       SIGFPE,      // GDB_SIGNAL_FPE  8
       SIGKILL,     // GDB_SIGNAL_KILL 9
       SIGBUS,      // GDB_SIGNAL_BUS  10
       SIGSEGV,     // GDB_SIGNAL_SEGV 11
       SIGSYS,      // GDB_SIGNAL_SYS  12
       SIGPIPE,     // GDB_SIGNAL_PIPE 13
       SIGALRM,     // GDB_SIGNAL_ALRM 14
       SIGTERM,     // GDB_SIGNAL_TERM 15
       SIGURG,      // GDB_SIGNAL_URG  16
       SIGSTOP,     // GDB_SIGNAL_STOP 17
       SIGTSTP,     // GDB_SIGNAL_TSTP 18
       SIGCONT,     // GDB_SIGNAL_CONT 19
       SIGCHLD,     // GDB_SIGNAL_CHLD 20
       SIGTTIN,     // GDB_SIGNAL_TTIN 21
       SIGTTOU,     // GDB_SIGNAL_TTOU 22
       SIGIO,       // GDB_SIGNAL_IO   23
       SIGXCPU,     // GDB_SIGNAL_XCPU 24
       SIGXFSZ,     // GDB_SIGNAL_XFSZ 25
       SIGVTALRM,   // GDB_SIGNAL_VTALRM 26
       SIGPROF,     // GDB_SIGNAL_PROF  27
       SIGWINCH,    // GDB_SIGNAL_WINCH 28
       0,           // GDB_SIGNAL_LOST  29
       SIGUSR1,     // GDB_SIGNAL_USR1  30
       SIGUSR2,     // GDB_SIGNAL_USR2  31
       SIGPWR,      // GDB_SIGNAL_PWR   32
       SIGPOLL,     // GDB_SIGNAL_POLL  33
   };

   if (gdb_signo > GDB_SIGNAL_POLL || gdbsig2linuxsig[gdb_signo] == 0) {
      // Just let the untranslatable values through
      km_infox(KM_TRACE_GDB, "%s: no linux signal for gdb signal %d", __FUNCTION__, gdb_signo);
      return gdb_signo;
   }
   return gdbsig2linuxsig[gdb_signo];
}

/*
 * Called from vcpu thread(s) after gdb-related kvm exit. Notifies gdbstub about the KVM exit and
 * then waits for gdbstub to allow vcpu to continue.
 * It is possible for multiple threads (vcpu's) to need gdb at about the same time,
 * so, we just take the thread that gets here first unless a non-debug related signal happens.  The
 * non-debug signal will take precedence over a debug signal.
 * If there is already a non-debug signal then the initial non-debug signal takes precedence.
 * We also see that some of the threads are just interrupted as result of the SIGUSR1 used to
 * stop a vcpu.  These threads never have precedence since they didn't hit a breakpoint nor
 * did they have an exception.
 * Parameters:
 *  vcpu - the thread/vcpu the signal fired on.
 *  signo - the linux signal number
 *  need_wait - set to true if the caller wants to wait until gdb tells us to go again.
 */
void km_gdb_notify_and_wait(km_vcpu_t* vcpu, int signo, bool need_wait)
{
   int rc;

   vcpu->is_paused = 1;

   rc = pthread_mutex_lock(&gdbstub.gdbnotify_mutex);
   assert(rc == 0);
   km_infox(KM_TRACE_GDB,
            "%s on VCPU %d, need_wait %d, session_requested %d, "
            "new signo %d, exit_reason %d, tid %d, "
            "existing signo %d, exit_reason %d, tid %d",
            __FUNCTION__,
            vcpu->vcpu_id,
            need_wait,
            gdbstub.session_requested,
            signo,
            vcpu->cpu_run->exit_reason,
            km_vcpu_get_tid(vcpu),
            gdbstub.signo,
            gdbstub.exit_reason,
            gdbstub.sigthreadid);

   if (gdbstub.session_requested == 0) {
      gdbstub.session_requested = 1;
      gdbstub.signo = gdb_signo(signo);
      gdbstub.sigthreadid = km_vcpu_get_tid(vcpu);
      gdbstub.exit_reason = vcpu->cpu_run->exit_reason;
      eventfd_write(machine.intr_fd, 1);   // wakeup the gdb server thread
   } else {
      // Already have a pending signal.  Decide if the new signal is more important.
      if ((vcpu->cpu_run->exit_reason != KVM_EXIT_DEBUG && gdbstub.exit_reason == KVM_EXIT_DEBUG)) {
         km_infox(KM_TRACE_GDB,
                  "%s: new signal %d for thread %d overriding pending signal %d for thread %d",
                  __FUNCTION__,
                  signo,
                  km_vcpu_get_tid(vcpu),
                  gdbstub.signo,
                  gdbstub.sigthreadid);
         gdbstub.signo = gdb_signo(signo);
         gdbstub.sigthreadid = km_vcpu_get_tid(vcpu);
         gdbstub.exit_reason = vcpu->cpu_run->exit_reason;
      }
   }
   rc = pthread_mutex_unlock(&gdbstub.gdbnotify_mutex);
   assert(rc == 0);

   if (need_wait != 0) {
      km_infox(KM_TRACE_GDB, "%s: vcpu %d waiting for gdb to let me run", __FUNCTION__, vcpu->vcpu_id);
      km_wait_on_gdb_cv(vcpu);   // Wait for gdb to allow this vcpu to continue
      vcpu->is_paused = 0;
      km_infox(KM_TRACE_GDB, "%s: gdb signalled for VCPU %d to continue", __FUNCTION__, vcpu->vcpu_id);
   }
}
