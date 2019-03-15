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
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"

gdbstub_info_t gdbstub;          // GDB global info
#define KM_SIGVCPUSTOP SIGUSR2   //  Used for signalling vcpu thread to force KVM exit
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
   km_infox("%s: Sending '%s'", __FUNCTION__, buffer);
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

/*
 * Convenience wrapper to send a signal to vcpu thread which will force KVM exit with KVM_EXIT_INTR.
 * (see signal handler setting kvm->immediate_exit)
 */
static int km_vcpu_force_exit(km_vcpu_t* vcpu)
{
   if (pthread_kill(vcpu->vcpu_thread, KM_SIGVCPUSTOP) != 0) {
      warn("%s: gdb has failed to signal guest about stopping vCPU %d", __FUNCTION__, vcpu->vcpu_id);
   }
   return 0;
}

// Force KVM_RUN to exit right away on ioctl entry, or re-entry from signal handling
static int km_vcpu_set_immediate_exit(km_vcpu_t* vcpu)
{
   vcpu->cpu_run->immediate_exit = 1;
   km_infox("%s: set immediate exit on a thread 0x%lx", __FUNCTION__, vcpu->vcpu_thread);
   return 0;
}

// sighandler, called on each vcpu (assuming pthread_kill was sent to each vcpu thread)
static void km_vcpustop_handler(int signum_unused)
{
   if (km_vcpu_apply_self(km_vcpu_set_immediate_exit) != 0) {
      err(2, "Failed to stop VCPU");
   }
}

/*
 * Handle individual KVM_RUN exit on vcpu.
 * Conducts dialog with gdb, until gdb orders next run (e.g. "next"),
 * at which points returns control.
 * Note: signum is upstairs converted from from KVM exit reason.
 */
static void gdb_handle_payload_stop(km_vcpu_t* vcpu, int signum)
{
   char* packet;
   char obuf[BUFMAX];

   /* Notify the debugger of our last signal */
   if (signum != GDB_SIGFIRST) {
      send_response('S', signum, true);
   }

   while ((packet = recv_packet()) != NULL) {
      km_gva_t addr = 0;
      km_kma_t kma;
      gdb_breakpoint_type_t type;
      size_t len;
      int command, ret;

      km_infox("%s: got packet: '%s'", __FUNCTION__, packet);
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
            /* Payload is single threaded for now, so accept all "set thread"
             * commands blindly and noop */
            send_okay_msg();
            break;
         }
         case 'R': {
            /* restart: kill run_vcpu thread(s) and run em again - TODO */
            /* for now: */ send_not_supported_msg();
            break;
         }
         case 's': {
            /* Step */
            if (sscanf(packet, "s%" PRIx64, &addr) == 1) {
               /* not supported, but that's OK as GDB will retry with the
                * slower version of this: update all registers. */
               send_not_supported_msg();
               break; /* Wait for another command. */
            }
            if (km_gdb_enable_ss(vcpu) == -1) {
               send_error_msg();
               break; /* Wait for another command. */
            }
            goto done; /* Continue with program */
         }
         case 'c': {
            /* Continue (and disable stepping for the next instruction) */
            if (sscanf(packet, "c%" PRIx64, &addr) == 1) {
               /* not supported, but that's OK as GDB will retry with the
                * slower version of this: update all registers. */
               send_not_supported_msg();
               break; /* Wait for another command. */
            }
            if (km_gdb_disable_ss(vcpu) == -1) {
               send_error_msg();
               break; /* Wait for another command. */
            }
            goto done; /* Continue with program */
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
            break; /* Wait for another command. */
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
            break; /* Wait for another command. */
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
            break; /* Wait for another command. */
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
               ret = km_gdb_add_breakpoint(vcpu, type, addr, len);
            } else {
               ret = km_gdb_remove_breakpoint(vcpu, type, addr, len);
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
#if 0
        case 'q': {
           /* TODO: separate command parsing into a sane table with "command" -> function */
           if (strncmp(packet, "qSupported", strlen("qSupported")) == 0) {
              send_packet("PacketSize=65535;qXfer:features:read+");
           } else if (strncmp(packet, "qXfer:features:read:target.xml:",
                              strlen("qXfer:features:read:target.xml:")) == 0) {
              send_packet(gdb_reg_info_xml);
           } else {
              send_not_supported_msg();
           }
           break; /* Wait for another command. */
        }
#endif
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
 * exited the run. If the exit reason is of interest to gdb, the function lets gdb try to handle it,
 * and returns 'true' for handled or 'false' for not handled. If the exit reason is of no interest
 * to gdb, the function  simply returns 'false' and lets vcpu loop to handle the exit as it sees
 * fit.
 *
 * Note that we map VM exit reason to a GDB 'signal', which is what needs to be communicated  back
 * to gdb client.
 */
static void km_gdb_handle_kvm_exit(km_vcpu_t* vcpu)
{
   assert(km_gdb_is_enabled());
   switch (vcpu->cpu_run->exit_reason) {
      case KVM_EXIT_HLT:
         send_response('W', 0, true);
         km_gdb_disable();
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

// returns 0 on success and 1 on failure. Failures just counted by upstairs for reporting
static inline int km_gdb_vcpu_continue(km_vcpu_t* vcpu)
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
   km_vcpu_t* vcpu = (km_vcpu_t*)data;
   gdbstub.intr_eventfd = eventfd(0, 0);

   struct pollfd fds[] = {
       {.fd = gdbstub.sock_fd, .events = POLLIN | POLLERR},
       {.fd = gdbstub.intr_eventfd, .events = POLLIN | POLLERR},
   };

   gdb_handle_payload_stop(vcpu, GDB_SIGFIRST);   // Talk to GDB first time, before any vCPU run
   eventfd_write(vcpu->eventfd, 1);               // Unblock main guest thread the first time
   while (km_gdb_is_enabled()) {
      if (poll(fds, sizeof(fds) / sizeof(struct pollfd), -1 /* no timeout */) < 0) {
         err(1, "%s: poll failed.", __FUNCTION__);
      }
      if (fds[1].revents) {   // vcpu stopped, we need to look at it
         eventfd_t unused;
         eventfd_read(gdbstub.intr_eventfd, &unused);   // clear up event sent to stop ^C wait
         // Wait for ALL vcpu to exit. TODO - take a lock
         for (int count = 0; count < machine.vm_vcpu_run_cnt; count++) {
            eventfd_read(gdbstub.sync_eventfd, &unused);   // Now wait for vcpu to signal exit
         }
         km_infox("%s: poll and vcpu_run done, got vcpu* (%p)", __FUNCTION__, vcpu);
         km_gdb_handle_kvm_exit(vcpu);   // give control to gdb
         km_vcpu_apply_all(km_gdb_vcpu_continue);
         continue;
      }
      if (fds[0].revents) {   // gdb client wants ^c
         int ch = recv_char();
         km_infox("%s: Checking recv and SKIPPING ^C or INTR: ch=%d", __FUNCTION__, ch);
         if (ch == -1) {
            warn("%s: connection closed", __FUNCTION__);
            km_gdb_disable();
            return NULL;
         }
         assert(errno == EINTR || ch == GDB_INTERRUPT_PKT);
         km_vcpu_apply_all(km_vcpu_force_exit);
         continue;
      }
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
   // First create all channels so the threads can communicate right away
   if ((gdbstub.sync_eventfd = eventfd(0, EFD_SEMAPHORE)) == -1) {
      err(1, "Failed to create event channel to talk to GDB threads");
   }
   if (pthread_mutex_init(&gdbstub.vcpu_lock, NULL) != 0) {
      err(1, "Failed to acquire mutex for gdb thread");
   }
   km_infox("Enabling gdbserver on port %d...", km_gdb_port_get());
   if (gdb_wait_for_connect(payload_file) == -1) {
      errx(1, "Failed to connect to gdb");
   }

   // TODO: think about 'attach' on signal - dynamically starting this thread
   if (pthread_create(&gdbstub.thread, NULL, km_gdb_thread_entry, km_main_vcpu()) != 0) {
      err(1, "Failed to create GDB server thread ");
   }
   km_install_sighandler(KM_SIGVCPUSTOP, km_vcpustop_handler);
}

void km_gdb_stop_stub(void)
{
   pthread_join(gdbstub.thread, NULL);
   close(gdbstub.sync_eventfd);
   pthread_mutex_destroy(&gdbstub.vcpu_lock);
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

/* Called on vcpu thread and waits until GDB allows the vcpu to continue running */
void km_gdb_prepare_for_run(km_vcpu_t* vcpu)
{
   eventfd_t buf;

   assert(km_gdb_is_enabled());
   // wait for gbd loop to allow vcpu to run
   eventfd_read(vcpu->eventfd, &buf);

   km_infox("%s: vcpu_run unblocked by gdb", __FUNCTION__);
}

/*
 * Called on each vcpu thread after gdb-related kvm exit. Calls gdbstub to handle the exit.
 */
void km_gdb_ask_stub_to_handle_kvm_exit(km_vcpu_t* vcpu, int run_errno)
{
   eventfd_t unused;

   assert(km_gdb_is_enabled());
   // TODO: do we need to reset eventfd on entry if it is filled in by multiple VCPUs?
   eventfd_write(gdbstub.intr_eventfd, 1);   // Interrupt gdbstub wait for ^C  (if it was waiting)
   eventfd_write(gdbstub.sync_eventfd, 1);   // Tell gdbstub to start interacting with client
   km_infox("%s: Waiting for gdb to allow vcpu %d to continue...", __FUNCTION__, vcpu->vcpu_id);
   eventfd_read(vcpu->eventfd, &unused);   // Wait for gdb to allow this vcpu to continue
   km_infox("%s: gdb signalled for this vcpu to continue", __FUNCTION__);
}