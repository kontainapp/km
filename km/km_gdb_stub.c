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

gdbstub_info_t gdbstub = {                  // GDB global info
   .sock_fd = -1,
   .gdbnotify_mutex = PTHREAD_MUTEX_INITIALIZER
};
#define BUFMAX (16 * 1024)                  // buffer for gdb protocol
static char in_buffer[BUFMAX];              // TODO: malloc/free these two
static unsigned char registers[BUFMAX];

#define GDB_ERROR_MSG "E01"   // The actual error code is ignored by GDB, so any number will do
static const char hexchars[] = "0123456789abcdef";

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

/* closes the gdb socket and set port to 0 */
static void km_gdb_disable(void)
{
   if (km_gdb_is_enabled() != 1) {
      return;
   }
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
 * This is a response to 'c' and 's' gdb packets. In other words, the VM was
 * running and it stopped for some reason. This message is to tell the
 * debugger that whe stopped (and why). The argument code can take these
 * and some other values:
 *    - 'S AA' received signal AA
 *    - 'W AA' exited with return code AA
 *    - 'X AA' exited with signal AA
 * https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html
 */
static void send_response(char code, int signum, bool wait_for_ack)
{
   char obuf[BUFMAX];

   snprintf(obuf, sizeof(obuf), "%c%02x", code, signum);
   if (wait_for_ack) {
      send_packet(obuf);
   } else {
      send_packet_no_ack(obuf);
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
      snprintf(label, sizeof(label), "Guest 0x%lx, %s",       // guest pthread pointer in free form label and reason for stopping
         vcpu->guest_thr,
         exit_reason);
      mem2hex((unsigned char*)label, obuf, strlen(label));
      send_packet(obuf);
   } else {
      send_not_supported_msg();
   }
}

/*
 * A Structure to hold a snapshot of the event that is waking the gdb server up.
 */
struct gdb_event {
   int signo;
   int sigthreadid;
   int exit_reason;
};
typedef struct gdb_event gdb_event_t;

/*
 * Handle KVM_RUN exit.
 * Conduct dialog with gdb, until gdb orders next run (e.g. "next"), at which points return
 * control so the payload may continue to run.
 *
 * Note: Before calling this function, KVM exit_reason is converted to signum.
 * TODO: split this function into a sane table-driven set of handlers based on parsed command.
 */
static void gdb_handle_payload_stop(gdb_event_t *gep)
{
   char* packet;
   char obuf[BUFMAX];
   km_vcpu_t* vcpu;
   int signo;

   km_infox(KM_TRACE_GDB, "%s: signum %d, sigthreadid %d, exit_reason %d", __FUNCTION__,
      gep->signo, gep->sigthreadid, gep->exit_reason);
   if (gep->signo != GDB_SIGFIRST) {   // Notify the debugger about our last signal
      send_response('S', gep->signo, true);

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
               /*
                * We just accept the signal number for now but don't do anything with it.
                * The signal is still queued so it will be delivered.
                * We are also ignoring the optional addr arg.
                */
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
            if (km_gdb_enable_ss() == -1) {
               send_error_msg();
               break;
            }
            goto done;   // Continue with program
         }
         case 'C': {
            if (sscanf(packet, "C%02x", &signo) == 1) {
               /*
                * We just accept the signal number for now but don't do anything with it.
                * The signal is still queued so it will be delivered.
                * We are also ignoring the optional addr arg.
                */
            }
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
            send_response('S', gep->signo, true);
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
      // gdb client interrupted the gdb server
      gep->signo = SIGINT;
      gdb_handle_payload_stop(gep);
      return;
   }
   switch (gep->exit_reason) {
      case KVM_EXIT_DEBUG:
         gep->signo = SIGTRAP;
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

   while (gdbstub.session_requested == 0 && (ret = eventfd_write(vcpu->gdb_efd, 1) == -1 && errno == EINTR)) {
      ;   // ignore signals during the write
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
   gdb_handle_payload_stop(&ge);   // Talk to GDB first time, before any vCPU run
   km_gdb_vcpu_continue(main_vcpu, 0);
   while (km_gdb_is_enabled() == 1) {
      int ret;
      int is_intr;   // set to 1 if we were interrupted by gdb client

      // Poll two fds described above in fds[], with no timeout ("-1")
      while ((ret = poll(fds, 2, -1) == -1) && (errno == EAGAIN || errno == EINTR)) {
         ;   // ignore signals which may interrupt the poll
      }
      if (ret < 0) {
         err(1, "%s: poll failed ret=%d.", __FUNCTION__, ret);
      }
      if (machine.vm_vcpu_run_cnt == 0) {
         send_response('W', ret, true);   // inferior normal exit
         km_gdb_disable();
         return;
      }
      machine.pause_requested = 1;
      km_infox(KM_TRACE_GDB, "%s: Signalling vCPUs to pause", __FUNCTION__);
      km_vcpu_apply_all(km_vcpu_pause, 0);
      km_vcpu_wait_for_all_to_pause();
      km_infox(KM_TRACE_GDB, "%s: vCPUs paused. run_cnt %d", __FUNCTION__, machine.vm_vcpu_run_cnt);
      is_intr = 0;            // memorize if GDB sent a ^C  or it was a vCPU kvm exit
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
          * a different thread id.  Then we set the vcpu using that threadid.
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
 */
static inline int gdb_signo(int sig)
{
   switch (sig) {
      case SIGUSR1:
         sig = 30;
         break;
      case SIGUSR2:
         sig = 31;
         break;
      case SIGCHLD:
         sig = 20;
         break;
      case SIGCONT:
         sig = 19;
         break;
      case SIGSTOP:
         sig = 17;
         break;
      case SIGTTIN:
         sig = 21;
         break;
      case SIGTTOU:
         sig = 22;
         break;
      case SIGBUS:
         sig = 10;
         break;
      case SIGSYS:
         sig = 12;
         break;
      case SIGURG:
         sig = 16;
         break;
      case SIGXCPU:
         sig = 24;
         break;
      case SIGXFSZ:
         sig = 25;
         break;
      case SIGIO:
         sig = 23;
         break;
   }
   return sig;
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
 */
void km_gdb_notify_and_wait(km_vcpu_t* vcpu, int signo)
{
   int rc;

   vcpu->is_paused = 1;

   rc = pthread_mutex_lock(&gdbstub.gdbnotify_mutex);
   assert(rc == 0);
   km_infox(KM_TRACE_GDB,
         "%s on VCPU %d, session_requested %d, "
         "new signo %d, exit_reason %d, tid %d, "
         "existing signo %d, exit_reason %d, tid %d",
         __FUNCTION__, vcpu->vcpu_id, gdbstub.session_requested,
         signo, vcpu->cpu_run->exit_reason, km_vcpu_get_tid(vcpu),
         gdbstub.signo, gdbstub.exit_reason, gdbstub.sigthreadid);

   if (gdbstub.session_requested == 0) {
      gdbstub.session_requested = 1;
      gdbstub.signo = gdb_signo(signo);
      gdbstub.sigthreadid = km_vcpu_get_tid(vcpu);
      gdbstub.exit_reason = vcpu->cpu_run->exit_reason;
      eventfd_write(machine.intr_fd, 1);     // wakeup the gdb server thread
   } else {
      // Already have a pending signal.  Decide if the new signal is more important.
      if ((vcpu->cpu_run->exit_reason != KVM_EXIT_DEBUG && gdbstub.exit_reason == KVM_EXIT_DEBUG)) {
         km_infox(KM_TRACE_GDB, "%s: new signal %d for thread %d overriding pending signal %d for thread %d",
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

   km_wait_on_eventfd(vcpu->gdb_efd);   // Wait for gdb to allow this vcpu to continue
   vcpu->is_paused = 0;
   km_infox(KM_TRACE_GDB, "%s: gdb done, VCPU %d continuing", __FUNCTION__, vcpu->vcpu_id);
}
