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
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "km.h"
#include "km_fork.h"
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

gdbstub_info_t gdbstub = {
    // GDB global info
    .sock_fd = -1,
    .notify_mutex = PTHREAD_MUTEX_INITIALIZER,
    .event_queue = TAILQ_HEAD_INITIALIZER(gdbstub.event_queue),
};
#define BUFMAX (16 * 1024)       // buffer for gdb protocol
static char in_buffer[BUFMAX];   // TODO: malloc/free these two
static unsigned char registers[BUFMAX];

#define GDB_ERROR_MSG "E01"   // The actual error code is ignored by GDB, so any number will do
static const_string_t hexchars = "0123456789abcdef";

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
unsigned char* hex2mem(const char* buf, unsigned char* mem, size_t count)
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
 * global gdbstub.listen_socket_fd and returns 0.
 * Returns -1 in case of failures.
 */
int km_gdb_setup_listen(void)
{
   int listen_socket_fd;
   struct sockaddr_in server_addr;
   int opt = 1;

   assert(gdbstub.port != 0);
   assert(gdbstub.listen_socket_fd == -1);
   listen_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_socket_fd == -1) {
      km_warn_msg("Could not create socket");
      return -1;
   }
   if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
      km_warn_msg("setsockopt(SO_REUSEADDR) failed");
   }
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   server_addr.sin_port = htons(km_gdb_port_get());

   if (bind(listen_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
      km_warn_msg("bind failed");
      close(listen_socket_fd);
      return -1;
   }
   if (listen(listen_socket_fd, 1) == -1) {
      km_warn_msg("listen failed");
      close(listen_socket_fd);
      return -1;
   }

   gdbstub.listen_socket_fd = listen_socket_fd;
   return 0;
}

void km_gdb_destroy_listen(void)
{
   if (gdbstub.listen_socket_fd != -1) {
      close(gdbstub.listen_socket_fd);
      gdbstub.listen_socket_fd = -1;
   }
}

static int km_gdb_accept_connection(void)
{
   struct in_addr ip_addr;
   struct sockaddr_in client_addr;
   socklen_t len;
   int opt = 1;

   len = sizeof(client_addr);
   gdbstub.sock_fd = accept(gdbstub.listen_socket_fd, (struct sockaddr*)&client_addr, &len);
   if (gdbstub.sock_fd == -1) {
      if (errno == EINVAL) {   // process exit caused a shutdown( listen_socket_fd, SHUT_RD )
         return EINVAL;
      }
      km_warn_msg("accept failed");
      return -1;
   }

   if (setsockopt(gdbstub.sock_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
      km_warn_msg("Setting TCP_NODELAY failed, continuing...");
   }
   ip_addr.s_addr = client_addr.sin_addr.s_addr;
   km_warn_msgx("Connection from debugger at %s", inet_ntoa(ip_addr));
   return 0;
}

/*
 * When the payload exits we need to get the km main thread out of the accept()
 * call in km_gdb_accept_connection().  Call this function to have that
 * happen.
 */
void km_gdb_accept_stop(void)
{
   int rc;

   km_infox(KM_TRACE_GDB, "Stop accepting new gdb client connections");
   if (gdbstub.gdb_client_attached == 0) {
      rc = shutdown(gdbstub.listen_socket_fd, SHUT_RD);
      if (rc != 0) {
         km_info(KM_TRACE_GDB, "shutdown on listening socket failed");
      }
   }
}

static void km_gdb_destroy_connection(void)
{
   close(gdbstub.sock_fd);
   gdbstub.sock_fd = -1;
}

/*
 * Do whatever is need to ensure the vcpu is runnable.
 * Essentially all of gdb's tenticles should be removed from the vcpu.
 * Always returns 0.
 */
static int km_gdb_vcpu_disengage(km_vcpu_t* vcpu, uint64_t unused)
{
   vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_RUNNING;
   gdbstub.stepping = 0;
   km_gdb_update_vcpu_debug(vcpu, 0);
   return 0;
}

/*
 * Initialize gdb's per vcpu state.
 * Called when a vcpu (a thread) comes to life.
 * You do want to called this when a parked vcpu is reused.
 */
void km_gdb_vcpu_state_init(km_vcpu_t* vcpu)
{
   vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_RUNNING;
   vcpu->gdb_vcpu_state.event.entry_is_active = 0;
}

static void gdb_fd_garbage_collect(void);

/*
 * Reset gdb state in the child process that results from a payload
 * fork() system call.  The parent is connected to the gdb client.
 * The child is not connected to the gdb client.
 */
void km_gdb_fork_reset(void)
{
   if (gdbstub.sock_fd >= 0) {
      close(gdbstub.sock_fd);
      gdbstub.sock_fd = -1;
   }
   if (gdbstub.listen_socket_fd >= 0) {
      close(gdbstub.listen_socket_fd);
      gdbstub.listen_socket_fd = -1;
   }
   gdbstub.enabled = 0;
   gdbstub.gdb_client_attached = 0;
   gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_UNSPECIFIED;
   gdbstub.session_requested = 0;
   TAILQ_INIT(&gdbstub.event_queue);
   gdb_fd_garbage_collect();
   // start listening on a different port again????
}

/*
 * Cleanup gdb state on gdb client detach.
 */
static void km_gdb_detach(void)
{
   km_gdb_remove_all_breakpoints();

   gdb_fd_garbage_collect();

   // Disconnect gdb from all of the vcpu's
   km_vcpu_apply_all(km_gdb_vcpu_disengage, 0);

   gdbstub.gdb_client_attached = 0;

   km_warn_msgx("gdb client disconnected");
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
      km_gdb_detach();
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
         km_warn_msgx("gdb message too long, disconnecting");
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
         km_warn_msgx("Failed checksum from GDB. "
                      "Calculated= 0x%x, received=0x%x. buf=%s",
                      checksum,
                      xmitcsum,
                      buffer);
         if (send_char('-') == -1) {
            // Unsuccessful reply to a failed checksum
            km_warn_msgx("GDB: Could not send an ACK- to the debugger.");
            return NULL;
         }
      } else {
         km_infox(KM_TRACE_GDB, "'%s', ack", buffer);
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

/*
 * Append src to the string described by dest and destmaxlen, growing the string
 * if necessary and then update *dest and *destmaxlen if the string was made bigger.
 * The destination can be null initially signifying the destination is empty.
 * The src string is assumed to be null terminated.
 * Returns:
 *  true - src was successfully appened to *dest.
 *  false - src was not appended because the string could not be grown to hold src
 */
static bool string_append(char** dest, size_t* destmaxlen, char* src)
{
   size_t curdestlen = (*dest != NULL) ? strlen(*dest) : 0;
   size_t cursrclen = strlen(src);

   if (*dest == NULL || (*destmaxlen - curdestlen) < (cursrclen + 1)) {   // not big enough, grow
                                                                          // the destionation string
      size_t bigger_size;
      char* bigger_dest;

      // Keep doubling the size until the destination can hold the source string
      bigger_size = *destmaxlen;
      if (bigger_size == 0) {
         bigger_size = 1024;
      }
      while (bigger_size - curdestlen < cursrclen + 1) {
         bigger_size += bigger_size;
      }
      bigger_dest = realloc(*dest, bigger_size);
      if (bigger_dest == NULL) {
         km_infox(KM_TRACE_GDB, "Failed to grow string at %p to %lu bytes", dest, bigger_size);
         return false;
      }
      km_infox(KM_TRACE_GDB, "String at %p grew from %lu to %lu bytes", dest, *destmaxlen, bigger_size);
      *dest = bigger_dest;
      *destmaxlen = bigger_size;
   }
   memcpy(&(*dest)[curdestlen], src, cursrclen);
   (*dest)[curdestlen + cursrclen] = 0;

   return true;
}

/*
 * Escape any special characters }, $, #, and * in the input buffer.
 * The buffer will expand in place if any chars are escaped.
 * Parameters:
 *   buffer - the buffer containing characters
 *   buflen - address of the number of bytes in the buffer.
 *      On return this will contain the size after any characters have been escaped.
 *   bufmax - the maximum number of characters that can be put in buffer.
 * Returns:
 *   0 - success
 *   -1 - there was not enough space in buffer to expand all special chars. The
 *      buffer is not cleaned up.
 */
static int gdb_add_escapes(uint8_t* buffer, size_t* buflen, size_t bufmax)
{
   int i;

   for (i = 0; i < *buflen; i++) {
      if (buffer[i] == '}' || buffer[i] == '$' || buffer[i] == '#' || buffer[i] == '*') {
         if ((bufmax - *buflen) == 0) {
            return -1;
         }
         memmove(&buffer[i + 1], &buffer[i], *buflen - i);
         buffer[i] = '}';
         buffer[i + 1] ^= 0x20;
         (*buflen)++;
         i++;
      }
   }
   return 0;
}

/*
 * Store a 32 bit int into a buffer in big endian order.
 */
static void put_uint32(uint8_t* b, uint32_t value)
{
   b[0] = (value >> 24);
   b[1] = (value >> 16);
   b[2] = (value >> 8);
   b[3] = value;
}

/*
 * Store a 64 bit int into a buffer in big endian order.
 */
static void put_uint64(uint8_t* b, uint64_t value)
{
   b[0] = (value >> 56);
   b[1] = (value >> 48);
   b[2] = (value >> 40);
   b[3] = (value >> 32);
   b[4] = (value >> 24);
   b[5] = (value >> 16);
   b[6] = (value >> 8);
   b[7] = value;
}

static void send_binary_packet_no_ack(const char* buffer, size_t buflen)
{
   unsigned char checksum;
   int count;

   km_infox(KM_TRACE_GDB, "sending binary reply, length %ld", buflen);
   // TODO trace hex dump of the reply.
   for (count = 0, checksum = 0; count < buflen; count++) {
      checksum += buffer[count];
   }
   send_char('$');
   send(gdbstub.sock_fd, buffer, buflen, 0);
   send_char('#');
   send_char(hexchars[checksum >> 4]);
   send_char(hexchars[checksum % 16]);
}

/*
 * Send a packet in binary.  The caller is expected to have already escaped
 * '$', $#', '}', and '*'.
 */
static void send_binary_packet(const char* buffer, size_t buflen)
{
   for (char ch = '\0'; ch != '+';) {
      send_binary_packet_no_ack(buffer, buflen);
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
               km_infox(KM_TRACE_GDB, "don't know how to handle exit_reason %d", gep->exit_reason);
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
 * A helper function to discover what kind of hw breakpoint fired
 * and the address that caused it.
 */
static int km_gdb_get_hwbreak_info(km_vcpu_t* vcpu, void** addr, uint32_t* type)
{
   struct kvm_debug_exit_arch* archp = &vcpu->cpu_run->debug.arch;

   if ((archp->dr6 & 1) != 0 && (archp->dr7 & 0x03) != 0) {
      /* breakpoint in dr0 fired. */
      *addr = (void*)vcpu->dr_regs[0];
      *type = (archp->dr7 >> 16) & 0x03;
   } else if ((archp->dr6 & 2) != 0 && (archp->dr7 & 0x0c) != 0) {
      /* breakpoint in dr1 fired. */
      *addr = (void*)vcpu->dr_regs[1];
      *type = (archp->dr7 >> 20) & 0x03;
   } else if ((archp->dr6 & 4) != 0 && (archp->dr7 & 0x20) != 0) {
      /* breakpoint in dr2 fired */
      *addr = (void*)vcpu->dr_regs[2];
      *type = (archp->dr7 >> 24) & 0x03;
   } else if ((archp->dr6 & 8) != 0 && (archp->dr7 & 0xc0) != 0) {
      /* breakpoint in dr3 fired */
      *addr = (void*)vcpu->dr_regs[3];
      *type = (archp->dr7 >> 28) & 0x03;
   } else {
      return -1;
   }
   return 0;
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
   int ret;

   /*
    * The gdb client is running the 'g' packet anyway so it is always
    * getting the registers we were supplying with the stop packet.
    */
   km_read_registers(vcpu);   // Make sure we report good register contents.

   km_infox(KM_TRACE_VCPU,
            "debug exception, exception 0x%x, pc 0x%llX, dr6 0x%llx, dr7 0x%llx",
            archp->exception,
            archp->pc,
            archp->dr6,
            archp->dr7);
   km_info(KM_TRACE_GDB,
           "sp %016llx, fp %016llx, pc %016llx",
           vcpu->regs.rsp,
           vcpu->regs.rbp,
           vcpu->regs.rip);

   /*
    * Apparently this is how a breakpoint (int 3) looks from KVM_EXIT_DEBUG: exception 3 on Intel,
    * and exc == 1 and both drs 0 on AMD. Note that on Intel the latter never happens.
    */
   if (archp->exception == BP_VECTOR ||
       (archp->dr6 == 0 && archp->dr7 == 0 && archp->exception == DB_VECTOR)) {
      sprintf(stopreply, "T05thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   }

   /*
    * We have hit a hardware breakpoint but the client can't accept a hwbreak
    * stop reply.  So, we send the client a trap stop reply.
    * This is probably happening because a hardware single step was performed.
    */
   if (archp->exception == DB_VECTOR && gdbstub.clientsup_hwbreak == 0) {
      assert((archp->dr6 & 0x4000) != 0);   // verify that this is a hw single step
      sprintf(stopreply, "T05thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   }

   if ((archp->dr6 & 0x4000) != 0) {   // Single step exception.
      /* Not sure if hwbreak is the right action for single step */
      sprintf(stopreply, "T05hwbreak:;thread:%08x;", km_vcpu_get_tid(vcpu));
      return;
   } else {
      ret = km_gdb_get_hwbreak_info(vcpu, &addr, &type);
      if (ret != 0) {
         km_infox(KM_TRACE_GDB, "triggered hw breakpoint doesn't match the set hw breakpoints");
         abort();
      }
      // We've hit a hw breakpoint and addr and type have been set.
   }
   km_info(KM_TRACE_VCPU, "addr %p, type 0x%x", addr, type);

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
           "vContSupported+;"
           "qXfer:auxv:read+;"
           "qXfer:exec-file:read+;"
           "qXfer:libraries-svr4:read+",
           BUFMAX - 1);
   send_packet(obuf);
}

/*
 * Storage area for a thread list in xml.
 * It will look like this:
 *  <?xml version="1.0"?>
 *  <threads>
 *    <thread id="id" core="0x0" name="name" handle="handle">
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
 * sizeof("  <thread id=\"%08x\" core=\"0x%08x\" name=\"%s\">\n  </thread>\n")
 */
const int MAX_THREADLISTENTRY_SIZE = 512;

static int build_thread_list_entry(km_vcpu_t* vcpu, uint64_t data)
{
   bool worked;
   char threadname[MAX_THREADNAME_SIZE];
   char threadlistentry[MAX_THREADLISTENTRY_SIZE];

   km_lock_vcpu_thr(vcpu);
   if (vcpu->is_active != 0) {
      km_getname_np(vcpu->vcpu_thread, threadname, sizeof(threadname));
   } else {
      // This thread is not fully instantiated.
      km_unlock_vcpu_thr(vcpu);
      return 0;
   }
   km_unlock_vcpu_thr(vcpu);

   snprintf(threadlistentry,
            sizeof(threadlistentry),
            "  <thread id=\"%08x\" core=\"0x%08x\" name=\"%s\">\n"
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
         km_warn_msgx("qGetTLSAddr: VCPU for thread %#x is not found", threadid);
         send_error_msg();
      }
   } else {
      // not enough args, protocol violation
      send_error_msg();
   }
}

/*
 * Return the number of chars in look_for_these are present in the input
 * memory pointed to by s.  The block of memory pointed to by s is sl
 * bytes in length.
 */
static uint32_t gdb_char_counter(uint8_t* s, uint32_t sl, char* look_for_these)
{
   int i;
   int j;
   uint32_t count = 0;

   for (i = 0; i < sl; i++) {
      for (j = 0; look_for_these[j] != 0; j++) {
         if (s[i] == look_for_these[j]) {
            count++;
            break;
         }
      }
   }
   return count;
}

/*
 * Handle the "qXfer:auxv:read" request.
 */
static uint8_t* gdb_auxv_copy;
static uint32_t gdb_auxv_len;

static void handle_qxfer_auxv_read(char* packet, char* obuf)
{
   int offset;
   int length;
   int n;
   size_t bytes_to_deliver = 0;

   n = sscanf(packet, "qXfer:auxv:read::%x,%x", &offset, &length);
   if (n == 2) {
      if (offset == 0) {
         // Build a copy of the auxv
         if (gdb_auxv_copy != NULL) {
            free(gdb_auxv_copy);
         }
         gdb_auxv_len =
             machine.auxv_size +
             gdb_char_counter(machine.auxv, machine.auxv_size, GDB_REMOTEPROTO_SPECIALCHARS);
         gdb_auxv_copy = malloc(gdb_auxv_len);
         if (gdb_auxv_copy == NULL) {
            send_error_msg();
            km_infox(KM_TRACE_GDB, "Couldn't allocate auxv buffer, len %d", gdb_auxv_len);
            return;
         }

         // Make our copy of auvx with special gdb chars escaped.
         memcpy(gdb_auxv_copy, machine.auxv, machine.auxv_size);
         size_t buflen = machine.auxv_size;
         if (gdb_add_escapes(gdb_auxv_copy, &buflen, machine.auxv_size) != 0) {
            send_error_msg();
            free(gdb_auxv_copy);
            gdb_auxv_copy = NULL;
            km_infox(KM_TRACE_GDB, "Couldn't escape special chars in auxv");
            return;
         }
      }
      if (offset >= gdb_auxv_len) {
         obuf[0] = 'l';
         obuf[1] = 0;
      } else {
         bytes_to_deliver = gdb_auxv_len - offset;
         if (bytes_to_deliver > length) {
            bytes_to_deliver = length;
            obuf[0] = 'm';
         } else {
            obuf[0] = 'l';
         }
         memcpy(&obuf[1], &gdb_auxv_copy[offset], bytes_to_deliver);
      }
   } else {
      // not enough args, protocol violation
      send_error_msg();
   }
   send_binary_packet(obuf, bytes_to_deliver + 1);
}

static char* gdb_execfile_name;
static uint32_t gdb_execfile_len;

static void handle_qxfer_execfile_read(const char* packet, char* obuf)
{
   int32_t offset;
   int32_t length;

   if (strncmp(packet, "qXfer:exec-file:read::", strlen("qXfer:exec-file:read::")) == 0) {
      if (sscanf(packet, "qXfer:exec-file:read::%x,%x", &offset, &length) != 2) {
         send_error_msg();
         return;
      }
   } else {
      /*
       * We aren't handling pid yet.
       * Since we don't claim to support multi-process debugging we shouldn't see a pid.
       */
      km_infox(KM_TRACE_GDB, "annex containing a pid is not supported");
      send_not_supported_msg();
      return;
   }
   if (offset == 0) {
      // Generate the absolute path
      if (gdb_execfile_name != NULL) {
         free(gdb_execfile_name);
      }
      size_t buflen = strlen(km_guest.km_filename);
      gdb_execfile_len =
          1 + buflen +
          gdb_char_counter((uint8_t*)km_guest.km_filename, buflen, GDB_REMOTEPROTO_SPECIALCHARS);
      gdb_execfile_name = malloc(gdb_execfile_len);
      if (gdb_execfile_name == NULL) {
         send_error_msg();
         km_infox(KM_TRACE_GDB, "Couldn't allocate %u bytes for the execfile name", gdb_execfile_len);
         return;
      }
      strcpy(gdb_execfile_name, km_guest.km_filename);
      if (gdb_add_escapes((uint8_t*)gdb_execfile_name, &buflen, gdb_execfile_len) != 0) {
         send_error_msg();
         free(gdb_execfile_name);
         gdb_execfile_name = NULL;
         km_infox(KM_TRACE_GDB, "Failed to escape special chars in execfile name");
         return;
      }
   }
   if (offset > gdb_execfile_len) {
      obuf[0] = 'l';
      obuf[1] = 0;
   } else {
      uint32_t bytes_to_deliver = gdb_execfile_len - offset;
      if (bytes_to_deliver > length) {
         bytes_to_deliver = length;
         obuf[0] = 'm';
      } else {
         obuf[0] = 'l';
      }
      memcpy(&obuf[1], &gdb_execfile_name[offset], bytes_to_deliver);
      obuf[1 + bytes_to_deliver] = 0;
   }
   send_packet(obuf);
}

/*
 * Handler for the "qXfer:libraries-svr4:read:annex:offset,length" command.
 * We should produce a response similar to:
 *  <library-list-svr4 version="1.0" main-lm="0x777777">
 *    <library name="/lib/ld-linux.so.2" lm="0x88888" l_addr="0x1111111" l_ld="0x222222"/>
 *    <library name="/lib/libc.so.6" lm="0x9999" l_addr="0xaaaaaa" l_ld="0xbbbbbb"/>
 *  </library-list-svr4>
 */
#define GDB_LIBRARY_DESC_LEN 200
typedef struct gdb_linkmap_arg {
   uint8_t count;
   char** bufp;
   size_t* bufl;
} gdb_linkmap_arg_t;

/*
 * Visitor function for traversing the list of dynamically loaded libraries.
 * This function is called once for each library.
 */
static int km_gdb_linkmap_visit(link_map_t* kmap, link_map_t* gvap, void* argp)
{
   gdb_linkmap_arg_t* lmargp = (gdb_linkmap_arg_t*)argp;
   int worked;
   char temp[GDB_LIBRARY_DESC_LEN + PATH_MAX];
   char* libname = (char*)km_gva_to_kma((km_gva_t)kmap->l_name);
   char dynlinker_absolute[PATH_MAX];
   uint8_t is_dynlinker;

   is_dynlinker = (strcmp(libname, KM_DYNLINKER_STR) == 0);
   if (is_dynlinker != 0) {
      if (km_dynlinker_file[0] != '/') {
         if (getcwd(dynlinker_absolute, sizeof(dynlinker_absolute)) == NULL) {
            km_info(KM_TRACE_GDB, "getcwd() failied");
            return 1;
         }
         snprintf(&dynlinker_absolute[strlen(dynlinker_absolute)],
                  sizeof(dynlinker_absolute) - strlen(dynlinker_absolute),
                  "/%s",
                  km_dynlinker_file);
      } else {
         snprintf(dynlinker_absolute, sizeof(dynlinker_absolute), "%s", km_dynlinker_file);
      }
   }

   if (lmargp->count == 0) {
      snprintf(temp,
               sizeof(temp),
               "<library-list-svr4 version=\"1.0\" main-lm=\"0x%lx\">\n",
               (uint64_t)gvap);
   } else {
      snprintf(temp,
               sizeof(temp),
               "  <library name=\"%s\" lm=\"%p\" l_addr=\"0x%lx\" l_ld=\"0x%lx\"/>\n",
               is_dynlinker != 0 ? dynlinker_absolute : libname,
               gvap,
               kmap->l_addr,
               kmap->l_ld);
   }
   worked = string_append(lmargp->bufp, lmargp->bufl, temp);
   if (worked == false) {
      km_infox(KM_TRACE_GDB, "Out of buffer space producing libraries-svr4 list");
      return 1;
   }
   lmargp->count++;

   return 0;
}

/*
 * The current list of libraries.
 */
static char* gdb_libsvr4_listp;
static size_t gdb_libsvr4_listl;

/*
 * Process the qXfer:libraries-svr4:read command.
 * We traverse the list of libraries and return a list of the information
 * from the link_map for each of the found libraries.
 */
static void handle_qxfer_librariessvr4_read(const char* packet, char* obuf)
{
   uint32_t offset;
   uint32_t length;
   uint32_t bytes_to_deliver;

   if (sscanf(packet, "qXfer:libraries-svr4:read::%x,%x", &offset, &length) != 2) {
      km_infox(KM_TRACE_GDB, "Error parsing qXfer:libraries-svr4:read");
      send_error_msg();
      return;
   }

   // Build up a new library list if starting from zero.
   if (offset == 0) {
      gdb_linkmap_arg_t lmstate = {
          .count = 0,
          .bufp = &gdb_libsvr4_listp,
          .bufl = &gdb_libsvr4_listl,
      };
      if (gdb_libsvr4_listp != NULL) {
         gdb_libsvr4_listp[0] = 0;
      }
      int rc = km_link_map_walk(km_gdb_linkmap_visit, &lmstate);
      if (rc != 0) {   // not enough space to hold the library list
         km_infox(KM_TRACE_GDB, "Error while producing library list");
         send_error_msg();
         return;
      }
      if (lmstate.count == 0) {   // nothing there
         string_append(&gdb_libsvr4_listp, &gdb_libsvr4_listl, "<library-list-svr4 version=\"1.0\" />");
      } else {
         string_append(&gdb_libsvr4_listp, &gdb_libsvr4_listl, "</library-list-svr4>");
      }
   }

   // Send a chunk of the library list.
   if (offset >= gdb_libsvr4_listl) {   // asking for beyond the end of the list
      obuf[0] = 'l';
      obuf[1] = 0;
   } else {
      bytes_to_deliver = gdb_libsvr4_listl - offset;
      if (bytes_to_deliver > length) {
         bytes_to_deliver = length;
         obuf[0] = 'm';
      } else {
         obuf[0] = 'l';
      }
      memcpy(&obuf[1], &gdb_libsvr4_listp[offset], bytes_to_deliver);
      obuf[bytes_to_deliver + 1] = 0;
   }
   send_packet(obuf);
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
         km_warn_msgx("qThreadExtraInfo: wrong packet '%s'", packet);
         send_error_msg();
         return;
      }
      if ((vcpu = km_vcpu_fetch_by_tid(thread_id)) == NULL) {
         km_warn_msgx("qThreadExtraInfo: VCPU for thread %#x is not found", thread_id);
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
   } else if (strncmp(packet, "qXfer:auxv:read::", strlen("qXfer:auxv:read::")) == 0) {
      handle_qxfer_auxv_read(packet, obuf);
   } else if (strncmp(packet, "qXfer:exec-file:read:", strlen("qXfer:exec-file:read:")) == 0) {
      handle_qxfer_execfile_read(packet, obuf);
   } else if (strncmp(packet, "qXfer:libraries-svr4:read:", strlen("qXfer:libraries-svr4:read:")) == 0) {
      handle_qxfer_librariessvr4_read(packet, obuf);
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
   gdb_thread_state_t ta_newrunstate;
   km_gva_t ta_steprange_start;   // if ta_newrunstate is THREADSTATE_RANGESTEPPING, the beginning
                                  // of the range
   km_gva_t ta_steprange_end;     // the end of the range stepping address range
};
typedef struct threadaction threadaction_t;

struct threadaction_blob {
   threadaction_t threadaction[KVM_MAX_VCPUS];   // each thread is bound to a virtual cpu
   uint32_t running;
   uint32_t stepping;
   uint32_t paused;
};
typedef struct threadaction_blob threadaction_blob_t;

/*
 * Assign a threadstate to those threads that have not had it set.
 */
static int km_gdb_set_default_runstate(km_vcpu_t* vcpu, uint64_t ta)
{
   int i = vcpu->vcpu_id;
   threadaction_blob_t* threadactionblob = (threadaction_blob_t*)ta;

   if (threadactionblob->threadaction[i].ta_newrunstate == THREADSTATE_NONE) {
      threadactionblob->threadaction[i].ta_newrunstate = THREADSTATE_PAUSED;
   }
   return 0;
}

/*
 * Apply the vCont action to the vcpu for each thread.
 * See comment in km_vcpu_handle_pause() for locking assumptions.
 */
static int km_gdb_set_thread_vcont_actions(km_vcpu_t* vcpu, uint64_t ta)
{
   int rc;
   int i = vcpu->vcpu_id;
   threadaction_blob_t* threadactionblob = (threadaction_blob_t*)ta;

   assert(vcpu->is_running == 0);
   switch (threadactionblob->threadaction[i].ta_newrunstate) {
      case THREADSTATE_NONE:
         km_infox(KM_TRACE_GDB, "vcpu %d no runstate set?", i);
         rc = -1;
         break;
      case THREADSTATE_RUNNING:
         vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_RUNNING;
         gdbstub.stepping = false;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      case THREADSTATE_RANGESTEPPING:
         vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_RANGESTEPPING;
         vcpu->gdb_vcpu_state.steprange_start = threadactionblob->threadaction[i].ta_steprange_start;
         vcpu->gdb_vcpu_state.steprange_end = threadactionblob->threadaction[i].ta_steprange_end;
         gdbstub.stepping = true;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      case THREADSTATE_STEPPING:
         vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_STEPPING;
         gdbstub.stepping = true;
         rc = km_gdb_update_vcpu_debug(vcpu, 0);
         break;
      case THREADSTATE_PAUSED:
         vcpu->gdb_vcpu_state.gdb_run_state = THREADSTATE_PAUSED;
         rc = 0;
         break;
      default:
         rc = -1;
         km_infox(KM_TRACE_GDB,
                  "vcpu %d, unhandled vcpu run state %d",
                  i,
                  threadactionblob->threadaction[i].ta_newrunstate);
         break;
   }
   km_infox(KM_TRACE_GDB,
            "vcpu %d: ta_newrunstate %d, gdb_run_state %d",
            i,
            threadactionblob->threadaction[i].ta_newrunstate,
            vcpu->gdb_vcpu_state.gdb_run_state);
   return rc;
}

/*
 * km_vcpu_apply_all() helper function to count how many threads are in
 * each gdb runstate.
 */
static int km_gdb_count_thread_states(km_vcpu_t* vcpu, uint64_t ta)
{
   int i = vcpu->vcpu_id;
   threadaction_blob_t* threadactionblob = (threadaction_blob_t*)ta;

   switch (threadactionblob->threadaction[i].ta_newrunstate) {
      case THREADSTATE_PAUSED:
         threadactionblob->paused++;
         break;
      case THREADSTATE_STEPPING:
      case THREADSTATE_RANGESTEPPING:
         threadactionblob->stepping++;
         break;
      case THREADSTATE_RUNNING:
         threadactionblob->running++;
         break;
      default:
         km_infox(KM_TRACE_GDB,
                  "thread %d, unhandled thread state %d",
                  km_vcpu_get_tid(vcpu),
                  threadactionblob->threadaction[i].ta_newrunstate);
         assert("unhandled gdb thread state" == NULL);
         break;
   }
   return 0;
}

static int linux_signo(gdb_signal_number_t);

static int verify_vcont(threadaction_blob_t* threadactionblob)
{
   threadactionblob->running = 0;
   threadactionblob->stepping = 0;
   threadactionblob->paused = 0;

   km_vcpu_apply_all(km_gdb_count_thread_states, (uint64_t)threadactionblob);

   // Ensure either 1 thread is stepping or 1 thread is running or all threads are running.
   if ((threadactionblob->running != 0 && threadactionblob->stepping != 0) ||
       ((threadactionblob->running + threadactionblob->stepping) > 1 && threadactionblob->paused != 0)) {
      km_infox(KM_TRACE_GDB,
               "Unsupported combination of running %d, stepping %d, and paused %d threads",
               threadactionblob->running,
               threadactionblob->stepping,
               threadactionblob->paused);
      return 1;
   }
   return 0;
}

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
      threadactionblob.threadaction[i].ta_newrunstate = THREADSTATE_NONE;
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
                     km_info(KM_TRACE_GDB, "tid %d is unknown?", tid);
                     send_error_msg();
                     return;
                  } else {
                     /* Use the id of the thread's vcpu as our index */
                     i = vcpu->vcpu_id;
                     if (threadactionblob.threadaction[i].ta_newrunstate == THREADSTATE_NONE) {
                        threadactionblob.threadaction[i] = (threadaction_t){
                            .ta_newrunstate = THREADSTATE_RANGESTEPPING,
                            .ta_steprange_start = startaddr,
                            .ta_steprange_end = endaddr,
                        };
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
                           km_info(KM_TRACE_GDB, "tid %d is unknown?", tid);
                           send_error_msg();
                           return;
                        } else {
                           /* Use the id of the thread's cpu as our index */
                           i = vcpu->vcpu_id;
                           if (threadactionblob.threadaction[i].ta_newrunstate == THREADSTATE_NONE) {
                              if (cmd == 'c' || cmd == 'C') {
                                 threadactionblob.threadaction[i].ta_newrunstate = THREADSTATE_RUNNING;
                              } else {
                                 threadactionblob.threadaction[i].ta_newrunstate = THREADSTATE_STEPPING;
                              }
                           } else {
                              /* An earlier action already specified what to do for this thread */
                           }
                           if (cmd == 'C' || cmd == 'S') {   // If a signal is sent, deliver it
                              siginfo_t info;

                              km_infox(KM_TRACE_GDB,
                                       "deliver linux signal %d (gdb %d) to thread %d",
                                       linuxsigno,
                                       gdbsigno,
                                       tid);
                              info.si_signo = linuxsigno;
                              info.si_code = SI_USER;
                              km_deliver_signal(vcpu, &info);
                           }
                        }
                     } else {
                        /* Not a valid number */
                        km_info(KM_TRACE_GDB, "%s is not a valid number?", tokenp1);
                        send_error_msg();
                        return;
                     }
                  } /* end of while tid list */
               } else if (tokenp[1] == 0) {
                  /* No tid list, the action applies to all threads with no action yet */
                  for (i = 0; i < KVM_MAX_VCPUS; i++) {
                     if (threadactionblob.threadaction[i].ta_newrunstate == THREADSTATE_NONE) {
                        if (tokenp[0] == 'c' || tokenp[0] == 'C') {
                           threadactionblob.threadaction[i].ta_newrunstate = THREADSTATE_RUNNING;
                        } else {
                           threadactionblob.threadaction[i].ta_newrunstate = THREADSTATE_STEPPING;
                        }
                     }
                  }
               } else {
                  // Some other trash after the action
                  km_infox(KM_TRACE_GDB, "unexpected stuff '%s' after action", tokenp);
                  send_error_msg();
                  return;
               }
               break;
            default:
               /* We don't support thread stop yet */
               km_info(KM_TRACE_GDB, "unsupported continue option '%s'", tokenp);
               send_not_supported_msg();
               return;
         }
      }
   } else {
      /* There must be an action. */
      km_info(KM_TRACE_GDB, "missing semicolon in %s'", packet);
      send_error_msg();
      return;
   }

   km_vcpu_apply_all(km_gdb_set_default_runstate, (uint64_t)&threadactionblob);

   /*
    * Verify that all threads are running or a single thread is stepping.
    */
   if (verify_vcont(&threadactionblob) != 0) {
      send_not_supported_msg();
      return;
   }

   /*
    * We made it through the vCont arguments, now apply what we were
    * asked to do.
    * Since km threads are each a vcpu, we just traverse the vcpus to
    * have each thread's vCont actions applied.
    */
   if ((count = km_vcpu_apply_all(km_gdb_set_thread_vcont_actions, (uint64_t)&threadactionblob)) != 0) {
      km_info(KM_TRACE_GDB, "apply all vcpus failed, count %d", count);
      send_error_msg();
   } else {
      *resume = true;
   }
   return;
}

/*
 * Meaning of the flag bits supplied in a gdb remote protocol file open request.
 */
#define GDB_O_RDONLY 0x0
#define GDB_O_WRONLY 0x1
#define GDB_O_RDWR 0x2
#define GDB_O_APPEND 0x8
#define GDB_O_CREAT 0x200
#define GDB_O_TRUNC 0x400
#define GDB_O_EXCL 0x800

/*
 * gdb errno values.
 */
#define GDB_EPERM 1
#define GDB_ENOENT 2
#define GDB_EINTR 4
#define GDB_EBADF 9
#define GDB_EACCES 13
#define GDB_EFAULT 14
#define GDB_EBUSY 16
#define GDB_EEXIST 17
#define GDB_ENODEV 19
#define GDB_ENOTDIR 20
#define GDB_EISDIR 21
#define GDB_EINVAL 22
#define GDB_ENFILE 23
#define GDB_EMFILE 24
#define GDB_EFBIG 27
#define GDB_ENOSPC 28
#define GDB_ESPIPE 29
#define GDB_EROFS 30
#define GDB_ENAMETOOLONG 91
#define GDB_EUNKNOWN 9999

static void km_gdb_vfile_init(void)
{
   int i;

   for (i = 0; i < MAX_GDB_VFILE_OPEN_FD; i++) {
      gdbstub.vfile_state.fd[i] = GDB_VFILE_FD_FREE_SLOT;
   }
}

void km_gdbstub_init(void)
{
   km_gdb_vfile_init();
   gdbstub.port = GDB_DEFAULT_PORT;
   gdbstub.listen_socket_fd = -1;
   gdbstub.enabled = 0;
   gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_UNSPECIFIED;
}

/*
 * Allocate a gdb file descriptor and return it.
 * If there are no free gdb file descriptors return -1;
 */
static int gdb_fd_alloc(void)
{
   int i;

   for (i = 0; i < MAX_GDB_VFILE_OPEN_FD; i++) {
      if (gdbstub.vfile_state.fd[i] == GDB_VFILE_FD_FREE_SLOT) {
         gdbstub.vfile_state.fd[i] = -2;
         return i;
      }
   }
   return -1;
}

/*
 * Free the passed gdb_fd.
 * If the gdb_fd is valid, free its slot and return 0.
 * If the gdb_fd is invalid or not open return an error.
 */
static int gdb_fd_free(int gdb_fd)
{
   if (gdb_fd < 0 || gdb_fd >= MAX_GDB_VFILE_OPEN_FD) {
      return EMFILE;
   }
   if (gdbstub.vfile_state.fd[gdb_fd] == GDB_VFILE_FD_FREE_SLOT) {
      return EBADF;
   }
   gdbstub.vfile_state.fd[gdb_fd] = GDB_VFILE_FD_FREE_SLOT;
   return 0;
}

/*
 * Return the linux fd that is bound to the passed gdb fd.
 * If the passed gdb fd is bad return -1.
 */
static int gdb_fd_find(int gdb_fd)
{
   if (gdb_fd >= 0 && gdb_fd < MAX_GDB_VFILE_OPEN_FD) {
      if (gdbstub.vfile_state.fd[gdb_fd] >= 0) {
         return gdbstub.vfile_state.fd[gdb_fd];
      }
   }
   return -1;
}

static void gdb_fd_set(int gdb_fd, int linux_fd)
{
   assert(gdb_fd >= 0 && gdb_fd < MAX_GDB_VFILE_OPEN_FD);
   assert(linux_fd >= 0);

   gdbstub.vfile_state.fd[gdb_fd] = linux_fd;
}

/*
 * Close all open fd's.
 * Called when a gdb client detachs from the target.  The
 * gdb clients are not good about closing open handles.
 */
static void gdb_fd_garbage_collect(void)
{
   int i;

   for (i = 0; i < MAX_GDB_VFILE_OPEN_FD; i++) {
      if (gdbstub.vfile_state.fd[i] != GDB_VFILE_FD_FREE_SLOT) {
         km_info(KM_TRACE_GDB, "Closing gdb vFile fd %d, linux fd %d", i, gdbstub.vfile_state.fd[i]);
         close(gdbstub.vfile_state.fd[i]);
         gdbstub.vfile_state.fd[i] = GDB_VFILE_FD_FREE_SLOT;
      }
   }
}

/*
 * Convert from linux errno values to gdb errno values.
 */
static int errno_linux2gdb(int linux_errno)
{
   int gdb_errno;

   switch (linux_errno) {
      case EPERM:
         gdb_errno = GDB_EPERM;
         break;
      case ENOENT:
         gdb_errno = GDB_ENOENT;
         break;
      case EINTR:
         gdb_errno = GDB_EINTR;
         break;
      case EBADF:
         gdb_errno = GDB_EBADF;
         break;
      case EACCES:
         gdb_errno = GDB_EACCES;
         break;
      case EFAULT:
         gdb_errno = GDB_EFAULT;
         break;
      case EBUSY:
         gdb_errno = GDB_EBUSY;
         break;
      case EEXIST:
         gdb_errno = GDB_EEXIST;
         break;
      case ENODEV:
         gdb_errno = GDB_ENODEV;
         break;
      case ENOTDIR:
         gdb_errno = GDB_ENOTDIR;
         break;
      case EISDIR:
         gdb_errno = GDB_EISDIR;
         break;
      case EINVAL:
         gdb_errno = GDB_EINVAL;
         break;
      case ENFILE:
         gdb_errno = GDB_ENFILE;
         break;
      case EMFILE:
         gdb_errno = GDB_EMFILE;
         break;
      case EFBIG:
         gdb_errno = GDB_EFBIG;
         break;
      case ENOSPC:
         gdb_errno = GDB_ENOSPC;
         break;
      case ESPIPE:
         gdb_errno = GDB_ESPIPE;
         break;
      case EROFS:
         gdb_errno = GDB_EROFS;
         break;
      case ENAMETOOLONG:
         gdb_errno = GDB_ENAMETOOLONG;
         break;
      default:
         gdb_errno = GDB_EUNKNOWN;
         break;
   }
   return gdb_errno;
}

/*
 * When the gdb server returns a binary string to the gdb client certain special
 * characters must be escaped.  This function copies the input to the output
 * and adds escape chars as needed.  When all of the input is copied and escaped
 * or the output buffer is filled we stop.
 * We return the size of the output produced and the amount of the input buffer
 * consumed.  The caller must handle cases where all of the input was not
 * processed.
 */
static int
gdb_binary_escape_add(char* input, int inputl, int* input_consumed, char* output, int outputl)
{
   int i;
   int o;

   i = 0;
   o = 0;
   while (i < inputl) {
      if (input[i] == '}' || input[i] == '#' || input[i] == '$' || input[i] == '*') {
         if (o + 2 > outputl) {   // there isn't enough space
            break;
         }
         output[o] = '}';
         output[o + 1] = input[i] ^ 0x20;
         o += 2;
      } else {
         if (o + 1 > outputl) {   // There isn't enough space
            break;
         }
         output[o] = input[i];
         o++;
      }
      i++;
   }
   *input_consumed = i;
   return o;
}

/*
 * The stat structure the gdb client wants to see.
 */
typedef struct __attribute__((__packed__)) gdb_client_stat {
   uint32_t st_dev;     /* device */
   uint32_t st_ino;     /* inode */
   uint32_t st_mode;    /* protection */
   uint32_t st_nlink;   /* number of hard links */
   uint32_t st_uid;     /* user ID of owner */
   uint32_t st_gid;     /* group ID of owner */
   uint32_t st_rdev;    /* device type (if inode device) */
   uint64_t st_size;    /* total size, in bytes */
   uint64_t st_blksize; /* blocksize for filesystem I/O */
   uint64_t st_blocks;  /* number of blocks allocated */
   uint32_t st_atimex;  /* time of last access */
   uint32_t st_mtimex;  /* time of last modification */
   uint32_t st_ctimex;  /* time of last change */
} gdb_client_stat_t;

static void handle_vfile_open(char* packet, char* output, int outputl)
{
   int n;
   int rc;
   int open_flags;
   int file_mode;
   char* commap;
   char filename[PATH_MAX / 2];
   int gdb_fd;
   int linux_fd;
   int gdb_errno;
   unsigned char* f;

   // Get the filename
   commap = strchr(packet, ',');
   if (commap == NULL) {
      send_error_msg();
      return;
   }
   *commap = 0;
   f = hex2mem(packet, (unsigned char*)filename, strlen(packet) / 2);
   *f = 0;
   *commap = ',';

   n = sscanf(commap + 1, "%x,%x", &open_flags, &file_mode);
   if (n == 2) {
      km_infox(KM_TRACE_GDB, "vfile open %s, flags 0x%x, mode 0x%x", filename, open_flags, file_mode);
      if (open_flags != GDB_O_RDONLY && open_flags != GDB_O_EXCL) {
         gdb_errno = GDB_EACCES;
         goto error_reply;
      }
      // See if we have any free fd's.
      gdb_fd = gdb_fd_alloc();
      if (gdb_fd < 0) {
         gdb_errno = GDB_ENFILE;
         goto error_reply;
      }
      /*
       * Form the path to open by prepending the contents of the symlink
       * /proc/XXX/root to the path supplied in the open request.
       */
      char open_filename[PATH_MAX];
      char procrootdirsymlink[100];
      snprintf(procrootdirsymlink,
               sizeof(procrootdirsymlink),
               "/proc/%d/root",
               gdbstub.vfile_state.current_fs);
      rc = readlink(procrootdirsymlink, open_filename, sizeof(open_filename) - 1);
      if (rc < 0) {
         gdb_fd_free(gdb_fd);
         gdb_errno = errno_linux2gdb(errno);
         km_info(KM_TRACE_GDB, "Couldn't read symlink %s", procrootdirsymlink);
         goto error_reply;
      }
      open_filename[rc] = 0;
      if (strcmp(open_filename, "/") == 0) {
         strcpy(open_filename, filename);
      } else {
         stringcat(open_filename, "/", sizeof(open_filename));
         stringcat(open_filename, filename, sizeof(open_filename));
      }
      linux_fd = open(open_filename, O_RDONLY);
      if (linux_fd < 0) {
         gdb_errno = errno_linux2gdb(errno);
         gdb_fd_free(gdb_fd);
         km_info(KM_TRACE_GDB, "Couldn't open %s", open_filename);
         goto error_reply;
      }
      gdb_fd_set(gdb_fd, linux_fd);
      sprintf(output, "F%08x", gdb_fd);
      send_packet(output);
      return;
   } else {
      gdb_errno = GDB_EINVAL;
      goto error_reply;
   }

error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

static void handle_vfile_close(char* packet, char* output, int outputl)
{
   int gdb_fd;
   int gdb_errno;
   int linux_fd;

   if (sscanf(packet, "%x", &gdb_fd) == 1) {
      linux_fd = gdb_fd_find(gdb_fd);
      if (linux_fd < 0) {
         gdb_errno = GDB_EBADF;
         goto error_reply;
      }
      close(linux_fd);
      gdb_fd_free(gdb_fd);
      sprintf(output, "F0");
      send_packet(output);
      return;
   } else {
      gdb_errno = GDB_EINVAL;
      goto error_reply;
   }

error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

static void handle_vfile_pread(char* packet, char* output, int outputl)
{
   int gdb_fd;
   int linux_fd;
   size_t count;
   off_t offset;
   int body_len;
   ssize_t bytes_read;
   int gdb_errno;

   if (sscanf(packet, "%x, %lx, %lx", &gdb_fd, &count, &offset) == 3) {
      linux_fd = gdb_fd_find(gdb_fd);
      if (linux_fd < 0) {
         gdb_errno = GDB_EBADF;
         goto error_reply;
      }
      const int VFILE_READ_PREFIX_SIZE = 10;
      if (count + VFILE_READ_PREFIX_SIZE > sizeof(registers)) {   // reduce read size to fit
                                                                  // available buffer
         km_infox(KM_TRACE_GDB,
                  "vFile:pread reducing read size from %ld to %ld bytes",
                  count,
                  sizeof(registers) - VFILE_READ_PREFIX_SIZE);
         count = sizeof(registers) - VFILE_READ_PREFIX_SIZE;
      }
      bytes_read = pread(linux_fd, registers, count, offset);
      if (bytes_read >= 0) {
         int bytes_copied;
         //  Reply format: Flength;attachment
         body_len = gdb_binary_escape_add((char*)registers,
                                          bytes_read,
                                          &bytes_copied,
                                          &output[VFILE_READ_PREFIX_SIZE],
                                          outputl - VFILE_READ_PREFIX_SIZE);
         sprintf(output, "F%08x", bytes_copied);
         output[VFILE_READ_PREFIX_SIZE - 1] = ';';
         send_binary_packet(output, VFILE_READ_PREFIX_SIZE + body_len);
         return;
      } else {
         gdb_errno = errno_linux2gdb(errno);
         goto error_reply;
      }
   } else {
      gdb_errno = GDB_EINVAL;
      goto error_reply;
   }
error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

static void handle_vfile_fstat(char* packet, char* output, int outputl)
{
   int gdb_fd;
   int linux_fd;
   struct stat statb;
   gdb_client_stat_t gdb_stat;
   int bytes_copied;
   int body_len;
   int prefix_len;
   int gdb_errno;

   if (sscanf(packet, "%x", &gdb_fd) == 1) {
      linux_fd = gdb_fd_find(gdb_fd);
      if (linux_fd < 0) {
         gdb_errno = GDB_EBADF;
         goto error_reply;
      }
      if (fstat(linux_fd, &statb) < 0) {
         gdb_errno = errno_linux2gdb(errno);
         goto error_reply;
      }
      put_uint32((uint8_t*)&gdb_stat.st_dev, 1);
      put_uint32((uint8_t*)&gdb_stat.st_ino, statb.st_ino);
      put_uint32((uint8_t*)&gdb_stat.st_mode, statb.st_mode);
      put_uint32((uint8_t*)&gdb_stat.st_nlink, statb.st_nlink);
      put_uint32((uint8_t*)&gdb_stat.st_uid, statb.st_uid);
      put_uint32((uint8_t*)&gdb_stat.st_gid, statb.st_gid);
      put_uint32((uint8_t*)&gdb_stat.st_rdev, statb.st_rdev);
      put_uint64((uint8_t*)&gdb_stat.st_size, statb.st_size);
      put_uint64((uint8_t*)&gdb_stat.st_blksize, statb.st_blksize);
      put_uint64((uint8_t*)&gdb_stat.st_blocks, statb.st_blocks);
      put_uint32((uint8_t*)&gdb_stat.st_atimex, statb.st_atime);
      put_uint32((uint8_t*)&gdb_stat.st_mtimex, statb.st_mtime);
      put_uint32((uint8_t*)&gdb_stat.st_ctimex, statb.st_ctime);

      sprintf(output, "F%08lx;", sizeof(gdb_stat));
      prefix_len = strlen(output);
      body_len = gdb_binary_escape_add((char*)&gdb_stat,
                                       sizeof(gdb_stat),
                                       &bytes_copied,
                                       &output[prefix_len],
                                       outputl - prefix_len);
      assert(bytes_copied == sizeof(gdb_stat));
      send_binary_packet(output, prefix_len + body_len);
      return;
   } else {
      gdb_errno = GDB_EINVAL;
      goto error_reply;
   }
error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

static void handle_vfile_readlink(char* packet, char* output, int outputl)
{
   char filename[PATH_MAX];
   unsigned char* f;
   int gdb_errno;
   int l;
   f = hex2mem(packet, (unsigned char*)filename, strlen(packet));
   *f = 0;
   if ((l = readlink(filename, (char*)registers, sizeof(registers) - 1)) < 0) {
      gdb_errno = errno_linux2gdb(errno);
      goto error_reply;
   }
   registers[l] = '\0';
   sprintf(output, "F0;");
   mem2hex(registers, &output[strlen(output)], l);
   send_packet(output);
   return;
error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

static void handle_vfile_setfs(char* packet, char* output, int outputl)
{
   pid_t pid;
   char* endptr;
   char filename[PATH_MAX];
   int gdb_errno;
   struct stat statb;
   /*
    * All we do here is verify that the pid is valid and remember the
    * pid for future open requests.
    * When an open happens, we just read the symlink /proc/pid/root and
    * prepend that to the path supplied with the open request.
    */
   pid = strtol(packet, &endptr, 16);
   if (*endptr != 0) {
      send_error_msg();
      return;
   }
   if (pid == 1) {   // external pid to our real pid
      pid = getpid();
   }
   if (pid == 0) {
      pid = getpid();
   }
   snprintf(filename, sizeof(filename), "/proc/%d/root", pid);
   if (stat(filename, &statb) != 0) {
      gdb_errno = errno_linux2gdb(errno);
      goto error_reply;
   }
   gdbstub.vfile_state.current_fs = pid;
   sprintf(output, "F0");
   send_packet(output);
   return;
error_reply:
   sprintf(output, "F-1,%08x", gdb_errno);
   send_packet(output);
}

/*
 * Handle gdb client requests to open and read files.
 * We currently do not support attempts to write or unlink files.
 */
static void km_gdb_handle_vfilepacket(char* packet, char* output, int outputl)
{
   char* p;
   int n;
   struct {
      char* cmd;
      void (*cmd_func)(char*, char*, int);
   } cmd_vector[] = {{"pread:", handle_vfile_pread},
                     {"fstat:", handle_vfile_fstat},
                     {"open:", handle_vfile_open},
                     {"close:", handle_vfile_close},
                     {"setfs:", handle_vfile_setfs},
                     {"readlink:", handle_vfile_readlink},
                     {NULL, NULL}};

   output[0] = 0;
   p = &packet[strlen("vFile:")];
   for (n = 0; cmd_vector[n].cmd != NULL; n++) {
      int l = strlen(cmd_vector[n].cmd);
      if (strncmp(p, cmd_vector[n].cmd, l) == 0) {
         (*cmd_vector[n].cmd_func)(&p[l], output, outputl);
         return;
      }
   }
   // We don't support pwrite or unlink yet.
   // Or, any other strange undocumented request the client sent us.
   send_not_supported_msg();
}

/*
 * The general v packet handler.
 * We currently handle:
 *  vCont? - gdb client finding out if we support vCont.
 *  vCont - gdb client tells us to continue or step instruction
 *  vFile - gdb remote file i/o requests
 */
static void km_gdb_handle_vpackets(char* packet, char* obuf, int obufl, int* resume)
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
   } else if (strncmp(packet, "vFile:", strlen("vFile:")) == 0) {
      /* gdb client wants us to run the app. */
      km_gdb_handle_vfilepacket(packet, obuf, obufl);
   } else {
      /* We don't support this. */
      send_not_supported_msg();
   }
}

/*
 * Find the oldest gdb event for a thread that is marked as running or stepping.
 * If such an event is found, delink it and return its address.
 * If no suitable event is found return NULL.
 */
static gdb_event_t* gdb_select_event(void)
{
   gdb_event_t* foundgep = NULL;
   /*
    * Walk through the gdb event queue oldest entry first.  Look for events that are coming from
    * a vcpu that is marked as running or stepping.  If we find such an event then deliver its
    * stop reply now.
    */
   km_mutex_lock(&gdbstub.notify_mutex);
   TAILQ_FOREACH (foundgep, &gdbstub.event_queue, link) {
      if (foundgep->signo == GDB_KMSIGNAL_DOFORK) {   // gdb needs to let a fork() hypercall happen
         TAILQ_REMOVE(&gdbstub.event_queue, foundgep, link);
         foundgep->entry_is_active = false;
         km_mutex_unlock(&gdbstub.notify_mutex);
         return foundgep;
      }
      if (foundgep->signo == GDB_KMSIGNAL_THREADEXIT) {
         send_response('S', foundgep, true);
         break;
      }
      km_vcpu_t* vcpu = km_vcpu_fetch_by_tid(foundgep->sigthreadid);
      if (vcpu->gdb_vcpu_state.gdb_run_state != THREADSTATE_PAUSED) {
         TAILQ_REMOVE(&gdbstub.event_queue, foundgep, link);
         km_mutex_unlock(&gdbstub.notify_mutex);
         km_infox(KM_TRACE_GDB,
                  "Selecting gdb event at %p, signo %d, threadid %d",
                  foundgep,
                  foundgep->signo,
                  foundgep->sigthreadid);
         km_gdb_vcpu_set(vcpu);
         send_response('S', foundgep, true);
         foundgep->entry_is_active = false;
         return foundgep;
      }
      km_infox(KM_TRACE_GDB,
               "Skipping event for paused vcpu: gep %p, signo %d, threadid %d, gdb_run_state %d",
               foundgep,
               foundgep->signo,
               foundgep->sigthreadid,
               vcpu->gdb_vcpu_state.gdb_run_state);
   }
   km_mutex_unlock(&gdbstub.notify_mutex);
   return NULL;
}

static int km_gdb_find_notpaused(km_vcpu_t* vcpu, uint64_t vcpuaddr)
{
   if (vcpu->gdb_vcpu_state.gdb_run_state == THREADSTATE_PAUSED) {
      return 0;
   } else {
      *((km_vcpu_t**)vcpuaddr) = vcpu;
   }
   return 1;
}

static km_vcpu_t* gdb_find_notpaused_vcpu(void)
{
   km_vcpu_t* vcpu = NULL;

   km_vcpu_apply_all(km_gdb_find_notpaused, (uint64_t)&vcpu);
   assert(vcpu != NULL);
   return vcpu;
}

/*
 * Conduct dialog with gdb, until gdb orders next run (e.g. "next"), at which points return
 * control so the payload may continue to run.
 *
 * Note: Before calling this function, KVM exit_reason is converted to signum.
 * TODO: split this function into a sane table-driven set of handlers based on parsed command.
 */
static void gdb_handle_remote_commands(gdb_event_t* gep)
{
   km_vcpu_t* vcpu = NULL;
   char* packet;
   char obuf[BUFMAX];
   int signo;

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
               km_warn_msgx("Wrong 'H' packet '%s'", packet);
               send_error_msg();
               break;
            }
            if (thread_id > 0 && (vcpu = km_vcpu_fetch_by_tid(thread_id)) == NULL) {
               km_warn_msgx("Can't find vcpu for tid %d (%#x) ", thread_id, thread_id);
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
            if (sscanf(packet, "S%02x", &signo) == 1) {
               siginfo_t info;

               // We should also check for the optional but unsupported addr argument.

               // Prevent out of range gdb signal numbers
               if (signo < 0 || signo > 44) {
                  send_error_msg();
                  break;
               }

               info.si_signo = linux_signo(signo);
               info.si_code = SI_USER;
               km_deliver_signal(vcpu, &info);
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
            // XXXX Set threadstate for all vcpu's
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
               km_deliver_signal(vcpu, &info);
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
            if (km_guest_mem2hex(addr, kma, obuf, len) != 0) {
               send_error_msg();
               break;
            }
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
            if (km_guest_hex2mem(obuf, len, kma) != 0) {
               send_error_msg();
               break;
            }
            send_okay_msg();
            break;
         }
         case 'g': {   // Read general registers
            km_vcpu_t* vcpu = km_gdb_vcpu_get();

            km_infox(KM_TRACE_GDB, "reporting registers for vcpu %d", vcpu->vcpu_id);
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
            km_warn_msgx("Debugger asked us to quit");
            send_okay_msg();
            km_err_msgx(1, "Quiting per debugger request");
            goto done;   // not reachable
         }
         case 'D': {   // Detach
            km_warn_msgx("Debugger detached");
            send_okay_msg();
            km_gdb_detach();
            goto done;
         }
         case 'q': {   // General query
            km_gdb_general_query(packet, obuf);
            break;
         }
         case 'v': {
            int resume;

            km_gdb_handle_vpackets(packet, obuf, sizeof(obuf), &resume);
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
done:;
}

// Read and discard pending eventfd reads, if any. Non-blocking.
void km_empty_out_eventfd(int fd)
{
   int flags = fcntl(fd, F_GETFL);
   fcntl(fd, F_SETFL, flags | O_NONBLOCK);
   km_wait_on_eventfd(fd);
   fcntl(fd, F_SETFL, flags);
}

static void gdb_delete_stale_events(void)
{
   gdb_event_t* gep;
   gdb_event_t* nextgep;
   km_vcpu_t* vcpu;

   km_mutex_lock(&gdbstub.notify_mutex);

   /*
    * Find events for breakpoints that were deleted by the user
    * before a pending gdb_event for the breakpoint was processed.
    * Delete the event so that gdb client doesn't need to
    * see this stale breakpoint trigger and discard it.
    */
   TAILQ_FOREACH_SAFE (gep, &gdbstub.event_queue, link, nextgep) {
      if ((vcpu = km_vcpu_fetch_by_tid(gep->sigthreadid)) == NULL) {
         continue;
      }
      if (vcpu->gdb_vcpu_state.gdb_run_state == THREADSTATE_RUNNING &&
          gep->signo == GDB_KMSIGNAL_KVMEXIT) {
         km_gva_t trigger_addr;
         uint32_t type;
         int ret;
         struct kvm_debug_exit_arch* archp = &vcpu->cpu_run->debug.arch;

         // Get the address that triggered this breakpoint
         if (archp->exception == BP_VECTOR ||
             (archp->dr6 == 0 && archp->dr7 == 0 && archp->exception == DB_VECTOR)) {
            /*
             * Software breakpoint has been hit.
             * On intel hardware, exception will be set to 3.
             * On AMD ryzen hardware, dr6 and dr7 are set to zero.
             */
            trigger_addr = vcpu->regs.rip;
         } else {   // Hardware breakpoint
            ret = km_gdb_get_hwbreak_info(vcpu, (void**)&trigger_addr, &type);
            if (ret != 0) {   // No hardware breakpoint triggered.
               continue;
            }
         }

         // Find the trigger address in the breakpoint list.
         gdb_breakpoint_type_t bptype;
         km_gva_t bpaddr;
         size_t bplen;
         ret = km_gdb_find_breakpoint(trigger_addr, &bptype, &bpaddr, &bplen);
         if (ret != 0) {   // No matching breakpoint, delete the event.
            km_infox(KM_TRACE_GDB,
                     "Deleted breakpoint, discard event: signo %d, sigthreadid %d, "
                     "trigger_addr 0x%lx",
                     gep->signo,
                     gep->sigthreadid,
                     trigger_addr);
            TAILQ_REMOVE(&gdbstub.event_queue, gep, link);
            gep->entry_is_active = false;
         }
      }
   }

   km_mutex_unlock(&gdbstub.notify_mutex);
}

void km_vcpu_resume_all(void)
{
   km_mutex_lock(&machine.pause_mtx);
   machine.pause_requested = 0;
   km_cond_broadcast(&machine.pause_cv);
   km_mutex_unlock(&machine.pause_mtx);
}

static void km_gdb_wait_for_dynlink_to_finish(void)
{
   Elf64_Addr payload_entry = km_guest.km_ehdr.e_entry + km_guest.km_load_adjust;
   /*
    * The dynamic linker is present and the user wants gdb to attach at _start
    * (not at entry to the dynamic linker).  So, we put a breakpoint on _start,
    * let the payload main thread run and then wait for the breakpoint to hit.
    * When the breakpoint fires, delete the breakpoint, delete the breakpoint
    * event and drop into the main part of gdbstub.
    */
   km_infox(KM_TRACE_GDB, "Begin waiting for dynamic linker to complete");
   if (km_gdb_add_breakpoint(GDB_BREAKPOINT_HW, payload_entry, 1) != 0) {
      km_err_msgx(3, "Failed to plant breakpoint on payload entry point 0x%lx", payload_entry);
   }
   gdbstub.gdb_client_attached = 1;       // this little hack gets us woken up when breakpoint fires
   km_vcpu_resume_all();                  // start the payload main thread
   km_wait_on_eventfd(machine.intr_fd);   // wait for the breakpoint we planted to be hit
   gdbstub.gdb_client_attached = 0;
   km_infox(KM_TRACE_GDB, "dynamic linker is done");
   if (km_gdb_remove_breakpoint(GDB_BREAKPOINT_HW, payload_entry, 1) != 0) {
      km_err_msgx(4, "Failed to remove breakpoint on payload entry point 0x%lx", payload_entry);
   }
   pthread_mutex_lock(&gdbstub.notify_mutex);
   gdb_event_t* gep = TAILQ_FIRST(&gdbstub.event_queue);
   assert(gep != NULL);
   if (gep->signo == GDB_KMSIGNAL_KVMEXIT) {
      // remove the breakpoint event.
      TAILQ_REMOVE(&gdbstub.event_queue, gep, link);
      gep->entry_is_active = false;
   }
   pthread_mutex_unlock(&gdbstub.notify_mutex);
}

/*
 * Loop on waiting on either ^C from gdb client, or a vcpu exit from kvm_run for a reason relevant
 * to gdb. When a wait is over (for either of the reasons), stops all vcpus and lets GDB handle the
 * exit. When gdb says "next" or "continue" or "step", signals vcpu to continue and reenters the loop.
 */
void km_gdb_main_loop(km_vcpu_t* main_vcpu)
{
   struct pollfd fds[] = {
       {.fd = -1, .events = POLLIN | POLLERR},
       {.fd = machine.intr_fd, .events = POLLIN | POLLERR},
   };
   int ret;

   km_wait_on_eventfd(machine.intr_fd);   // Wait for km_start_vcpus to be called

   assert(gdbstub.wait_for_attach != GDB_WAIT_FOR_ATTACH_UNSPECIFIED);
   if (km_dynlinker.km_filename != NULL && gdbstub.wait_for_attach == GDB_WAIT_FOR_ATTACH_AT_START) {
      km_gdb_wait_for_dynlink_to_finish();
   }

   if (gdbstub.wait_for_attach == GDB_DONT_WAIT_FOR_ATTACH) {
      km_vcpu_resume_all();
   } else {
      km_warn_msgx("Waiting for a debugger. Connect to it like this:");
      km_warn_msgx("\tgdb --ex=\"target remote localhost:%d\" %s\nGdbServerStubStarted\n",
                   km_gdb_port_get(),
                   km_guest.km_filename);
   }

accept_connection:;
   ret = km_gdb_accept_connection();
   if (ret == EINVAL) {   // all target threads have exited while we waited in accept()
      km_infox(KM_TRACE_GDB, "gdb listening socket is shutdown");
      return;
   }
   assert(ret == 0);   // not sure what to do with errors here yet.
   gdbstub.gdb_client_attached = 1;
   fds[0].fd = gdbstub.sock_fd;
   km_vcpu_pause_all();

   km_gdb_vcpu_set(main_vcpu);   // gdb default thread

   gdb_event_t ge = (gdb_event_t){
       .signo = 0,
       .sigthreadid = km_vcpu_get_tid(main_vcpu),
   };
   gdb_handle_remote_commands(&ge);   // Talk to GDB first time, before any vCPU run

   gdbstub.session_requested = 0;
   km_vcpu_resume_all();
   while (gdbstub.gdb_client_attached != 0) {
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
         km_gdb_detach();
         km_gdb_destroy_connection();
         return;
      }
      km_infox(KM_TRACE_GDB, "Signalling vCPUs to pause");
      km_vcpu_pause_all();
      km_infox(KM_TRACE_GDB, "vCPUs paused. run_cnt %d", machine.vm_vcpu_run_cnt);
      if (fds[0].revents) {   // got something from gdb client (hopefully ^C)
         int ch = recv_char();
         km_infox(KM_TRACE_GDB, "got a msg from a client. ch=%d", ch);
         if (ch == -1) {   // channel error or EOF (ch == -1)
            break;
         }
         assert(ch == GDB_INTERRUPT_PKT);   // At this point it's only legal to see ^C from GDB
         km_mutex_lock(&gdbstub.notify_mutex);
         /*
          * If a payload thread has already stopped it will have caused session_requested
          * to be set non-zero.  In this case we prefer to use the stopped thread as
          * opposed to the user ^C as the reason for breaking to gdb command level.
          */
         if (gdbstub.session_requested == 0) {
            gdbstub.session_requested = 1;
            ge = (gdb_event_t){
                .entry_is_active = true,
                .signo = GDB_SIGNAL_INT,
                .sigthreadid = km_vcpu_get_tid(gdb_find_notpaused_vcpu()),
            };
            TAILQ_INSERT_HEAD(&gdbstub.event_queue, &ge, link);
         }
         km_mutex_unlock(&gdbstub.notify_mutex);
      }
      if (fds[1].revents) {
         km_infox(KM_TRACE_GDB, "a vcpu signalled about a kvm exit");
      }
      km_empty_out_eventfd(machine.intr_fd);   // discard extra 'intr' events if vcpus sent them

      gdb_event_t* foundgep;
      while ((foundgep = gdb_select_event()) != NULL) {
         if (foundgep->signo == GDB_KMSIGNAL_DOFORK) {
            int in_child;
            km_dofork(&in_child);
            if (in_child != 0) {   // We are the child, just return to the main thread
               return;
            } else {   // We are the parent process, just pretend nothing happened
               continue;
            }
         }
         // process commands from the gdb client
         gdb_handle_remote_commands(foundgep);
         // Delete gdb_events for breakpoints that have been deleted
         gdb_delete_stale_events();
      }

      km_infox(KM_TRACE_GDB, "kvm exit handled, starting vcpu's");

      km_mutex_lock(&gdbstub.notify_mutex);   // Block vcpu events until all are started

      gdbstub.session_requested = 0;
      km_vcpu_resume_all();

      km_mutex_unlock(&gdbstub.notify_mutex);   // Allow vcpus to wakeup us now
   }

   km_gdb_destroy_connection();
   goto accept_connection;
}

/*
 * The musl library uses internal signal numbers for some operation.  If gdb client
 * is attached to km, these signals will be forwarded to the gdb client and the
 * gdb client just sends them back to the km gdb server.  We need to translate
 * these signal numbers to some signal number that is not part of the gdb signal
 * space.
 */
#ifndef SIGTIMER
#define SIGTIMER 32
#endif
#ifndef SIGCANCEL
#define SIGCANCEL 33
#endif
#ifndef SIGSYNCCALL
#define SIGSYNCCALL 34
#endif

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
       [SIGHUP] = GDB_SIGNAL_HUP,         // SIGHUP    1
       [SIGQUIT] = GDB_SIGNAL_INT,        // SIGINT    2
       [SIGQUIT] = GDB_SIGNAL_QUIT,       // SIGQUIT   3
       [SIGILL] = GDB_SIGNAL_ILL,         // SIGILL    4
       [SIGTRAP] = GDB_SIGNAL_TRAP,       // SIGTRAP   5
       [SIGABRT] = GDB_SIGNAL_ABRT,       // SIGABRT   6
       [SIGBUS] = GDB_SIGNAL_BUS,         // SIGBUS    7
       [SIGFPE] = GDB_SIGNAL_FPE,         // SIGFPE    8
       [SIGKILL] = GDB_SIGNAL_KILL,       // SIGKILL   9
       [SIGUSR1] = GDB_SIGNAL_USR1,       // SIGUSR1   10
       [SIGSEGV] = GDB_SIGNAL_SEGV,       // SIGSEGV   11
       [SIGUSR2] = GDB_SIGNAL_USR2,       // SIGUSR2   12
       [SIGPIPE] = GDB_SIGNAL_PIPE,       // SIGPIPE   13
       [SIGALRM] = GDB_SIGNAL_ALRM,       // SIGALRM   14
       [SIGTERM] = GDB_SIGNAL_TERM,       // SIGTERM   15
       [SIGSTKFLT] = 0,                   // SIGSTKFLT 16
       [SIGCHLD] = GDB_SIGNAL_CHLD,       // SIGCHLD   17
       [SIGCONT] = GDB_SIGNAL_CONT,       // SIGCONT   18
       [SIGSTOP] = GDB_SIGNAL_STOP,       // SIGSTOP   19
       [SIGTSTP] = GDB_SIGNAL_TSTP,       // SIGTSTP   20
       [SIGTTIN] = GDB_SIGNAL_TTIN,       // SIGTTIN   21
       [SIGTTOU] = GDB_SIGNAL_TTOU,       // SIGTTOU   22
       [SIGURG] = GDB_SIGNAL_URG,         // SIGURG    23
       [SIGXCPU] = GDB_SIGNAL_XCPU,       // SIGXCPU   24
       [SIGXFSZ] = GDB_SIGNAL_XFSZ,       // SIGXFSZ   25
       [SIGVTALRM] = GDB_SIGNAL_VTALRM,   // SIGVTALRM 26
       [SIGPROF] = GDB_SIGNAL_PROF,       // SIGPROF   27
       [SIGWINCH] = GDB_SIGNAL_WINCH,     // SIGWINCH  28
       [SIGIO] = GDB_SIGNAL_IO,           // SIGIO     29
       [SIGPOLL] = GDB_SIGNAL_POLL,       // SIGPOLL   29
       [SIGPWR] = GDB_SIGNAL_PWR,         // SIGPWR    30
       [SIGSYS] = GDB_SIGNAL_SYS,         // SIGSYS    31
       // The following are not really gdb signals.  They just let us xlate from musl
       // internal signals to values we can xlated back when the gdb client sends them
       // back to gdb server
       [SIGTIMER] = GDB_SIGNAL_REALTIME_34,      // SIGTIMER  32, arbitrarily chosen
       [SIGCANCEL] = GDB_SIGNAL_CANCEL,          // SIGCANCEL 33
       [SIGSYNCCALL] = GDB_SIGNAL_REALTIME_33,   // SIGSYNCCALL 34, arbitrarily chosen
   };

   if (linuxsig > sizeof(linuxsig2gdbsig) / sizeof(linuxsig2gdbsig[0]) ||
       linuxsig2gdbsig[linuxsig] == 0) {
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
       [0] = 0,
       [GDB_SIGNAL_HUP] = SIGHUP,                // GDB_SIGNAL_HUP  1
       [GDB_SIGNAL_INT] = SIGINT,                // GDB_SIGNAL_INT  2
       [GDB_SIGNAL_QUIT] = SIGQUIT,              // GDB_SIGNAL_QUIT 3
       [GDB_SIGNAL_ILL] = SIGILL,                // GDB_SIGNAL_ILL  4
       [GDB_SIGNAL_TRAP] = SIGTRAP,              // GDB_SIGNAL_TRAP 5
       [GDB_SIGNAL_ABRT] = SIGABRT,              // GDB_SIGNAL_ABRT 6
       [GDB_SIGNAL_EMT] = 0,                     // GDB_SIGNAL_EMT  7
       [GDB_SIGNAL_FPE] = SIGFPE,                // GDB_SIGNAL_FPE  8
       [GDB_SIGNAL_KILL] = SIGKILL,              // GDB_SIGNAL_KILL 9
       [GDB_SIGNAL_BUS] = SIGBUS,                // GDB_SIGNAL_BUS  10
       [GDB_SIGNAL_SEGV] = SIGSEGV,              // GDB_SIGNAL_SEGV 11
       [GDB_SIGNAL_SYS] = SIGSYS,                // GDB_SIGNAL_SYS  12
       [GDB_SIGNAL_PIPE] = SIGPIPE,              // GDB_SIGNAL_PIPE 13
       [GDB_SIGNAL_ALRM] = SIGALRM,              // GDB_SIGNAL_ALRM 14
       [GDB_SIGNAL_TERM] = SIGTERM,              // GDB_SIGNAL_TERM 15
       [GDB_SIGNAL_URG] = SIGURG,                // GDB_SIGNAL_URG  16
       [GDB_SIGNAL_STOP] = SIGSTOP,              // GDB_SIGNAL_STOP 17
       [GDB_SIGNAL_TSTP] = SIGTSTP,              // GDB_SIGNAL_TSTP 18
       [GDB_SIGNAL_CONT] = SIGCONT,              // GDB_SIGNAL_CONT 19
       [GDB_SIGNAL_CHLD] = SIGCHLD,              // GDB_SIGNAL_CHLD 20
       [GDB_SIGNAL_TTIN] = SIGTTIN,              // GDB_SIGNAL_TTIN 21
       [GDB_SIGNAL_TTOU] = SIGTTOU,              // GDB_SIGNAL_TTOU 22
       [GDB_SIGNAL_IO] = SIGIO,                  // GDB_SIGNAL_IO   23
       [GDB_SIGNAL_XCPU] = SIGXCPU,              // GDB_SIGNAL_XCPU 24
       [GDB_SIGNAL_XFSZ] = SIGXFSZ,              // GDB_SIGNAL_XFSZ 25
       [GDB_SIGNAL_VTALRM] = SIGVTALRM,          // GDB_SIGNAL_VTALRM 26
       [GDB_SIGNAL_PROF] = SIGPROF,              // GDB_SIGNAL_PROF  27
       [GDB_SIGNAL_WINCH] = SIGWINCH,            // GDB_SIGNAL_WINCH 28
       [GDB_SIGNAL_LOST] = 0,                    // GDB_SIGNAL_LOST  29
       [GDB_SIGNAL_USR1] = SIGUSR1,              // GDB_SIGNAL_USR1  30
       [GDB_SIGNAL_USR2] = SIGUSR2,              // GDB_SIGNAL_USR2  31
       [GDB_SIGNAL_PWR] = SIGPWR,                // GDB_SIGNAL_PWR   32
       [GDB_SIGNAL_POLL] = SIGPOLL,              // GDB_SIGNAL_POLL  33
       [GDB_SIGNAL_REALTIME_34] = SIGTIMER,      // GDB_SIGNAL_REALTIME_34
       [GDB_SIGNAL_CANCEL] = SIGCANCEL,          // GDB_SIGNAL_CANCEL
       [GDB_SIGNAL_REALTIME_33] = SIGSYNCCALL,   // GDB_SIGNAL_REALTIME_33
   };

   if (gdb_signo > sizeof(gdbsig2linuxsig) / sizeof(gdbsig2linuxsig[0]) ||
       gdbsig2linuxsig[gdb_signo] == 0) {
      // Just let the untranslatable values through
      km_infox(KM_TRACE_GDB, "no linux signal for gdb signal %d", gdb_signo);
      return gdb_signo;
   }
   return gdbsig2linuxsig[gdb_signo];
}

/*
 * Called from vcpu thread(s) after gdb-related kvm exit. Notifies gdbstub about the KVM exit and
 * then waits for gdbstub to allow vcpu to continue.
 * It is possible for multiple threads (vcpu's) to need gdb at about the same time,
 * so, we we always enqueue gdb events to the tail of the queue.  The gdb server can sort out
 * what to do with multiple events per gdbstub wake up.
 * Parameters:
 *  vcpu - the thread/vcpu the signal fired on.
 *  signo - the linux signal number
 *  need_wait - set to true if the caller wants to wait until gdb tells us to go again.
 */
void km_gdb_notify(km_vcpu_t* vcpu, int signo)
{
   km_mutex_lock(&machine.pause_mtx);
   machine.pause_requested = 1;
   km_mutex_unlock(&machine.pause_mtx);

   km_mutex_lock(&gdbstub.notify_mutex);
   km_infox(KM_TRACE_GDB,
            "session_requested %d, linux signo %d, exit_reason %u, tid %d, gdb event queue %d",
            gdbstub.session_requested,
            signo,
            vcpu->cpu_run->exit_reason,
            km_vcpu_get_tid(vcpu),
            TAILQ_EMPTY(&gdbstub.event_queue));

   // Enqueue this thread's gdb event on the tail of gdb event queue.
   assert(vcpu->gdb_vcpu_state.event.entry_is_active == false);
   vcpu->gdb_vcpu_state.event = (gdb_event_t){
       .entry_is_active = true,
       .signo = gdb_signo(signo),
       .sigthreadid = km_vcpu_get_tid(vcpu),
       .exit_reason = vcpu->cpu_run->exit_reason,
   };
   TAILQ_INSERT_TAIL(&gdbstub.event_queue, &vcpu->gdb_vcpu_state.event, link);

   // Wake up gdb server if there are no other pending gdb events.
   if (gdbstub.session_requested == 0) {
      gdbstub.session_requested = 1;
      eventfd_write(machine.intr_fd, 1);   // wakeup the gdb server thread
   }
   km_mutex_unlock(&gdbstub.notify_mutex);
}
