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

gdbstub_info_t gdbstub;          // GDB global info
#define BUFMAX (16 * 1024)       // buffer for gdb protocol
static char in_buffer[BUFMAX];   // TODO: malloc/free these two
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
static int gdb_wait_for_connect(const char* image_name)
{
   int listen_socket_fd;
   struct sockaddr_in server_addr, client_addr;
   struct in_addr ip_addr;
   socklen_t len;
   int opt = 1;

   assert(km_gdb_is_enabled());
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

   if ((ret = recv(gdbstub.sock_fd, &ch, 1, 0)) < 0) {
      return -1;
   }
   if (ret == 0) {
      warn("socket shutdown");   // orderly shutdown
      return -1;
   }
   /*
    * Could be either printable '$...' command, or ^C. We do not support 'X'
    * (binary data) packets
    */
   if (!(isprint(ch) || ch == GDB_INTERRUPT_PKT)) {
      warn("unexpected character 0x%x", ch);
      return -1;
   }
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
         km_infox("%s: '%s', ack", __FUNCTION__, buffer);
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
   km_infox("Sending packet '%s'", buffer);
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
         km_gdb_disable();
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

// Add hex vcpu->vcpu_id to the list of thread_ids for communication to gdb
static int add_thread_id(km_vcpu_t* vcpu, uint64_t data)
{
   char* obuf = (char*)data;
   sprintf(obuf + strlen(obuf), "%x,", vcpu->vcpu_id);
   return 0;
}

// Form and send thread list ('m<thread_ids>' packet) to gdb
static void send_threads_list(void)
{
   char obuf[BUFMAX] = "m";   // max thread is is 288 (currently) so BUFMAX is more than enough
   km_vcpu_apply_all(add_thread_id, (uint64_t)obuf);
   obuf[strlen(obuf) - 1] = '\0';   // strip trailing comma
   send_packet(obuf);
}

// handle general query packet ('q<query>'). Use obuf as output buffer.
static void km_gdb_general_query(char* packet, char* obuf)
{
   if (strncmp(packet, "qfThreadInfo", strlen("qfThreadInfo")) == 0) {   // Get list of active thread IDs
      send_threads_list();
   } else if (strncmp(packet, "qsThreadInfo", strlen("qsThreadInfo")) == 0) {   // Get more thread ids
      send_packet("l");   // 'l' means "no more thread ids"
   } else if (strncmp(packet, "qAttached", strlen("qAttached")) == 0) {
      send_packet("1");   // '1' means "the process was attached, not started"
   } else if (strncmp(packet, "qC", strlen("qC")) == 0) {   // Get the current thread_id
      char buf[64];
      sprintf(buf, "QC%x", gdbstub.vcpu_id);
      send_packet(buf);
   } else if (strncmp(packet, "qThreadExtraInfo", strlen("qThreadExtraInfo")) == 0) {   // Get label
      // gdb allow free form labels, so we send guest_thr
      int thr_id;
      km_vcpu_t* vcpu;
      char label[64];

      if (sscanf(packet, "qThreadExtraInfo,%x", &thr_id) != 1) {
         km_infox("qThreadExtraInfo: wrong packet '%s'", packet);
         send_error_msg();
         return;
      }
      if ((vcpu = km_vcpu_fetch(thr_id)) == NULL) {
         km_infox("qThreadExtraInfo: VCPU %d is not found", thr_id);
         send_error_msg();
         return;
      }
      sprintf(label, "THR %#lx", vcpu->guest_thr);
      mem2hex((unsigned char*)label, obuf, strlen(label));
      send_packet(obuf);
   } else {
      send_not_supported_msg();
   }
}
/*
 * Handle individual KVM_RUN exit on vcpu.
 * Conducts dialog with gdb, until gdb orders next run (e.g. "next"), at which points returns
 * control.
 *
 * Note: Before calling this function, KVM exit_reason is converted to signum.
 * TODO: split this function into a sane table-driven set of handlers based on parsed command.
 */
static void gdb_handle_payload_stop(km_vcpu_t* vcpu, int signum)
{
   char* packet;
   char obuf[BUFMAX];

   assert(vcpu != NULL);
   km_infox("%s: signum %d", __FUNCTION__, signum);
   if (signum != GDB_SIGFIRST) {   // Notify the debugger about our last signal
      send_response('S', signum, true);
   }
   while ((packet = recv_packet()) != NULL) {
      km_gva_t addr = 0;
      km_kma_t kma;
      gdb_breakpoint_type_t type;
      size_t len;
      int command, ret;

      km_infox("Got packet: '%s'", packet);
      /*
       * From the GDB manual:
       * "At a minimum, a stub is required to support the ‘g’ and ‘G’
       * commands for register access, and the ‘m’ and ‘M’ commands
       * for memory access. Stubs that only control single-threaded
       * targets can implement run control with the ‘c’ (continue),
       * and ‘s’ (step) commands."
       */
      command = packet[0];
      switch (command) {
         case '!': {
            /* allow extended-remote mode */
            send_okay_msg();
            break;
         }
         case 'H': {
            /* ‘H op thread-id’.
             * Set thread for subsequent operations. op should be ‘c’ for step and continue  and ‘g’
             * for other operations.
             * TODO: this is deprecated, supporting the ‘vCont’ command is a better option.
             *   See https://sourceware.org/gdb/onlinedocs/gdb/Packets.html#Packets)
             */
            int vcpu_id;
            char cmd_unused;
            if (sscanf(packet, "H%c%d", &cmd_unused, &vcpu_id) != 2) {
               send_error_msg();
               break;
            }
            /* If requested, change VCPU we are working on
             * Note: vcpu_id == -1 means "all threads". Use current vcpu for now.
             * TODO: not clear what it means for 'get regs' for example
             */
            if (vcpu_id != -1) {
               km_vcpu_t* tmp;
               if ((tmp = km_vcpu_fetch(vcpu_id)) == NULL) {
                  send_error_msg();
                  break;
               }
               vcpu = tmp;
               km_gdb_vcpu_id_set(vcpu_id);
            }
            send_okay_msg();
            break;
         }
         case 'T': {
            /* ‘T thread-id’.
             * Find out if the thread thread-id is alive.
             */
            int vcpu_id;
            if (sscanf(packet, "T%x", &vcpu_id) != 1 || km_vcpu_fetch(vcpu_id) == NULL) {
               km_infox("Reporting thread for vcpu %d as dead", vcpu_id);
               send_error_msg();
               break;
            }
            send_okay_msg();
            break;
         }
         case 's': {
            /* Step */
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
         case 'c': {
            /* Continue (and disable stepping for the next instruction) */
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
         case 'm': {
            /* Read memory content */
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
         case 'M': {
            /* Write memory content */
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
         case 'g': {
            /* Read general registers */
            len = BUFMAX;
            if (km_gdb_read_registers(vcpu, registers, &len) == -1) {
               send_error_msg();
               break;
            }
            mem2hex(registers, obuf, len);
            send_packet(obuf);
            break;
         }
         case 'G': {
            /* Write general registers */
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
            break; /* Wait for another command. */
         }
         case '?': {
            /* Return last signal */
            send_response('S', signum, true);
            break; /* Wait for another command. */
         }
         case 'Z':
            /* Insert a breakpoint */
         case 'z': {
            /* Remove a breakpoint */
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
         case 'k': {
            warnx("Debugger asked us to quit");
            send_okay_msg();
            errx(1, "Quiting per debugger request");
            goto done;   // not reachable
         }
         case 'D': {
            warnx("Debugger detached");
            send_okay_msg();
            km_gdb_disable();
            goto done;
         }
         case 'q': {
            km_gdb_general_query(packet, obuf);
            break;
         }
         default: {
            send_not_supported_msg();
            break;
         }
      }   // switch
   }      // while
   if (packet == NULL) {
      warnx("GDB: Stop debugging as we could not receive the next command "
            "from the debugger.");
      km_gdb_disable();
   }
done:
   return;
}

/*
 * This function is called on GDB thread to give gdb a chance to handle the KVM exit.
 * It is expected to be called ONLY when gdb is enabled and ONLY when the vcpu in question has
 * exited the run. If the exit reason is of interest to gdb, the function lets gdb try to handle
 * it, and returns 'true' for handled or 'false' for not handled. If the exit reason is of no
 * interest to gdb, the function  simply returns 'false' and lets vcpu loop to handle the exit as
 * it sees fit.
 *
 * Note that we map VM exit reason to a GDB 'signal', which is what needs to be communicated back
 * to gdb client.
 */
static void km_gdb_handle_kvm_exit(void)
{
   km_vcpu_t* vcpu = km_vcpu_fetch(km_gdb_vcpu_id_get());

   assert(km_gdb_is_enabled());
   switch (vcpu->cpu_run->exit_reason) {
      case KVM_EXIT_HLT:
         km_gdb_fini(0);
         return;

      case KVM_EXIT_DEBUG:
         gdb_handle_payload_stop(vcpu, SIGTRAP);
         return;

      case KVM_EXIT_INTR:
         gdb_handle_payload_stop(vcpu, SIGINT);
         return;

      case KVM_EXIT_EXCEPTION:
         gdb_handle_payload_stop(vcpu, SIGSEGV);
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

// returns 0 on success and 1 on failure. Failures just counted by upstairs for reporting
static inline int km_gdb_vcpu_continue(km_vcpu_t* vcpu, uint64_t unused)
{
   int ret;
   while ((ret = eventfd_write(vcpu->eventfd, 1) == -1 && errno == EINTR))
      ;
   return ret == 0 ? 0 : 1;   // Unblock main guest thread
}

/*
 * When vcpu is running, we still want to listen to gdb ^C, in which case signal
 * vcpu thread to interrupt KVM_RUN.
 */
static void* km_gdb_thread_entry(void* data)
{
   km_vcpu_t* main_vcpu = (km_vcpu_t*)data;
   gdbstub.intr_eventfd = eventfd(0, 0);
   struct pollfd fds[] = {
       {.fd = gdbstub.sock_fd, .events = POLLIN | POLLERR},
       {.fd = gdbstub.intr_eventfd, .events = POLLIN | POLLERR},
   };

   gdb_handle_payload_stop(main_vcpu, GDB_SIGFIRST);   // Talk to GDB first time, before any vCPU run
   km_gdb_vcpu_continue(main_vcpu, 0);
   while (km_gdb_is_enabled()) {
      int ret;

      // Poll 2 fds described above in fds[], with no timeout ("-1")
      while ((ret = poll(fds, 2, -1) == -1) && (errno == EAGAIN || errno == EINTR))
         ;
      if (ret < 0) {
         err(1, "%s: poll failed ret=%d.", __FUNCTION__, ret);
      }
      if (!km_gdb_is_enabled()) {
         break;   // gdb was disabled when it was sleeping; break the loop early
      }
      machine.pause_requested = 1;
      gdbstub.session_requested = 1;
      if (fds[1].revents) {   // vcpu stopped
         km_infox("gdb: a vcpu signalled about an exit");
         km_wait_on_eventfd(gdbstub.intr_eventfd);   // clear up event sent from vcpu
      }
      if (fds[0].revents) {   // gdb client sent something (hopefully ^C)
         int ch = recv_char();
         km_infox("%s: got a msg from a client checking for ^C or INTR: ch=%d", __FUNCTION__, ch);
         if (ch == -1) {
            warn("%s: connection closed", __FUNCTION__);
            km_gdb_disable();
            return NULL;
         }
         assert(errno == EINTR || ch == GDB_INTERRUPT_PKT);
      }

      km_infox("Signalling all vCPUs to pause");
      km_vcpu_apply_all(km_vcpu_pause, 0);
      km_vcpu_wait_for_all_to_pause();
      km_empty_out_eventfd(gdbstub.intr_eventfd);   // discard extra 'intr' events if vcpus sent them
      km_infox("%s: DONE waiting, all stopped. vm_vcpu_run_cnt %d", __FUNCTION__, machine.vm_vcpu_run_cnt);
      km_gdb_handle_kvm_exit();   // give control back to gdb
      gdbstub.session_requested = 0;
      machine.pause_requested = 0;
      km_infox("%s: exit handled, ready to proceed", __FUNCTION__);
      km_vcpu_apply_all(km_gdb_vcpu_continue, 0);
   }
   return NULL;
}

/*
 * Start gdb stub - wait for GDB to connect and start a thread to manage interaction with gdb
 * client.
 */
void km_gdb_start_stub(char* const payload_file)
{
   assert(km_gdb_is_enabled());
   km_infox("Enabling gdbserver on port %d...", km_gdb_port_get());
   if (gdb_wait_for_connect(payload_file) == -1) {
      errx(1, "Failed to connect to gdb");
   }

   // TODO: think about 'attach' on signal - dynamically starting this thread
   if (pthread_create(&gdbstub.thread, NULL, km_gdb_thread_entry, km_main_vcpu()) != 0) {
      err(1, "Failed to create GDB server thread ");
   }
}

void km_gdb_join_stub(void)
{
   pthread_join(gdbstub.thread, NULL);
   close(gdbstub.intr_eventfd);
}

/* closes the gdb socket and set port to 0 */
void km_gdb_disable(void)
{
   if (gdbstub.sock_fd > 0) {
      close(gdbstub.sock_fd);
      gdbstub.sock_fd = -1;
   }
   warnx("Disabling gdb");
   km_gdb_port_set(0);
}

/*
 * Called from vcpu thread(s) after gdb-related kvm exit. Notifies gdbstub about the KVM exit and
 * then waits for gdbstub to allow vcpu to continue.
 */
void km_gdb_notify_and_wait(km_vcpu_t* vcpu, int run_errno)
{
   km_infox("%s on VCPU %d", __FUNCTION__, vcpu->vcpu_id);
   vcpu->is_paused = 1;
   if (gdbstub.session_requested == 0) {
      km_infox("gdb seems to be sleeping, wake it up. VCPU %d", vcpu->vcpu_id);
      gdbstub.vcpu_id = vcpu->vcpu_id;
      eventfd_write(gdbstub.intr_eventfd, 1);
   }
   km_wait_on_eventfd(vcpu->eventfd);   // Wait for gdb to allow this vcpu to continue
   km_infox("%s: gdb signalled for VCPU %d to continue", __FUNCTION__, vcpu->vcpu_id);
   vcpu->is_paused = 0;
}

// Tell GBD we are done
void km_gdb_fini(int ret)
{
   if (!km_gdb_is_enabled()) {
      return;
   }
   send_response('W', ret, false);
   km_gdb_disable();
   eventfd_write(gdbstub.intr_eventfd, 1);
}