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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "km.h"
#include "km_gdb.h"
#include "km_signal.h"
#include "km_thread.h"
#include "chan/chan.h"

int g_gdb_port = 0;       // 0 means NO GDB

static int socket_fd = 0;
static const char hexchars[] = "0123456789abcdef";

#define BUFMAX (16 * 1024)
static char in_buffer[BUFMAX];       // TODO: malloc/free these two
static unsigned char registers[BUFMAX];

/* The actual error code is ignored by GDB, so any number will do. */
#define GDB_ERROR_MSG                  "E01"

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
static char *mem2hex(const unsigned char *mem, char *buf, size_t count)
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
static unsigned char *hex2mem(const char *buf,
                              unsigned char *mem, size_t count)
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
 * global socket_fd and returns 0.
 * Returns -1 in case of failures.
 */
static int gdb_wait_for_connect(const char *image_name)
{
   int listen_socket_fd;
   struct sockaddr_in server_addr, client_addr;
   struct in_addr ip_addr;
   socklen_t len;
   int opt = 1;

   assert(km_gdb_enabled());
   listen_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_socket_fd == -1) {
      warn("Could not create socket");
      return -1;
   }

   if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                  sizeof(opt)) == -1) {
      warn("setsockopt(SO_REUSEADDR) failed");
   }
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   server_addr.sin_port = htons(g_gdb_port);

   if (bind(listen_socket_fd, (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1) {
      warn("bind failed");
      return -1;
   }

   if (listen(listen_socket_fd, 1) == -1) {
      warn("listen failed");
      return -1;
   }

   warnx("Waiting for a debugger. Connect to it like this:");
   warnx("\tgdb --ex=\"target remote localhost:%d\" %s", g_gdb_port,
         image_name);

   len = sizeof(client_addr);
   socket_fd = accept(listen_socket_fd, (struct sockaddr *)&client_addr, &len);
   if (socket_fd == -1) {
      warn("accept failed");
      return -1;
   }
   close(listen_socket_fd);

   if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt,
                  sizeof(opt)) == -1) {
      warn("Setting TCP_NODELAY failed, continuing...");
   }
   ip_addr.s_addr = client_addr.sin_addr.s_addr;
   warnx("Connection from debugger at %s", inet_ntoa(ip_addr));
   return 0;
}

static int send_char(char ch)
{
    /* TCP is already buffering, so no need to buffer here as well. */
    return send(socket_fd, &ch, 1, 0);
}

/* get single char, blocking */
static char recv_char(void)
{
    unsigned char ch;
    int ret;

    if ((ret = recv(socket_fd, &ch, 1, 0)) < 0) {
       return -1;
    }
    if (ret == 0) {
       warn("socket shutdown");       // orderly shutdown
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
static char *recv_packet(void)
{
   char *buffer = &in_buffer[0];
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
      for (count = 0, checksum = 0; count < BUFMAX - 1;
           count++, checksum += ch) {
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
      buffer[count] = '\0';       // Make this a C string.

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
               checksum, xmitcsum, buffer);
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
static void send_packet_no_ack(const char *buffer)
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
    for (count = 0, checksum = 0; (ch = buffer[count]) != '\0';
         count++, checksum += ch)
       ;
    send(socket_fd, buffer, count, 0);
    send_char('#');
    send_char(hexchars[checksum >> 4]);
    send_char(hexchars[checksum % 16]);
}

/*
 * Send a packet and wait for a successful ACK of '+' from the debugger.
 * An ACK of '-' means that we have to resend.
 */
static void send_packet(const char *buffer)
{
   for (char ch = '\0'; ch != '+';) {
      send_packet_no_ack(buffer);
      if ((ch = recv_char()) == -1) {
         return;
      }
   }
}

#define send_error_msg()   do { send_packet(GDB_ERROR_MSG); } while (0)
#define send_not_supported_msg()   do { send_packet(""); } while (0)
#define send_okay_msg()   do { send_packet("OK"); } while (0)

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
 * Convenience wrapper to signal gdb thread that kvm run is done.
 * This will make gdb stub exit from poll() for ^C
 */
static void km_signal_gdb_about_kvm_exit(void)
{
   if (pthread_kill(g_km_threads.gdbsrv_thread, GDBSTUB_SIGNAL) != 0) {
      err(1, "Failed to inform GDB about VM run completion - pthread_kill() "
             "failed");
   }
}

/*
 * Convenience wrapper to signal vcpu thread that gdb stub got ^C from gdb client.
 * THis will make KVM to exit with KVM_EXIT_INTR
 *
 */
static void km_signal_vcpu_about_gdb_intr(void)
{
   if (pthread_kill(g_km_threads.vcpu_thread[0], GDBSTUB_SIGNAL) < 0) {
      err(1, "Failed to send signal to VCPU to wake it up");
   }
}

/*
 * When vcpu is running, we still want to listen to gdb ^C, in which case signal
 * vcpu thread to interrupt KVM_RUN.
 */
void km_gdb_poll_for_client_intr(km_vcpu_t *vcpu, int sig_fd)
{
   struct pollfd fds[] = {
       {.fd = socket_fd, .events = POLLIN | POLLERR},
       {.fd = sig_fd, .events = POLLIN | POLLERR | POLLRDHUP}};
   if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1 /* no timeout */) < 0) {
      // warn and fall through to notify that poll has exited
      warn("%s: poll failed.", __FUNCTION__);
   } else {
      if (fds[1].revents) {
         struct signalfd_siginfo info;

         km_infox("%s: Got signalfd, reading", __FUNCTION__);
         int ret = read(fds[1].fd, &info, sizeof(info));
         if (ret != sizeof(struct signalfd_siginfo)) {
            err(1, "read signalfd failed");
         }
         assert(info.ssi_signo == GDBSTUB_SIGNAL);
                  km_infox("%s: Got signalfd, exiting poll", __FUNCTION__);
      }
      if (fds[0].revents) {
         int ch = recv_char();
         km_infox("%s: Checking recv and SKIPPING ^C or INTR: ch=%d", __FUNCTION__, ch);
         assert(errno == EINTR || ch == GDB_INTERRUPT_PKT);
         km_signal_vcpu_about_gdb_intr();
      }
   }
}

/*
 * Handle individual KVM_RUN exit on vcpu.
 * Conducts dialog with gdb, until gdb orders next run (e.g. "next"),
 * at which points returns control.
 * Note: signum is upstairs converted from from KVM exit reason.
 */
static void gdb_handle_payload_stop(km_vcpu_t *vcpu, int signum)
{
   char *packet;
   char obuf[BUFMAX];

   /* Notify the debugger of our last signal */
   if (signum != GDB_SIGFIRST) {
      send_response('S', signum, true);
   }

   while ((packet = recv_packet()) != NULL) {
      km_gva_t addr = 0;
      void *kma;       // KM virtual address
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
            /* restart: kill run_vcpu thread(s) and run em again - TBD */
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
            mem2hex(kma, obuf, len);
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
            void *kma = km_gva_to_kma(addr);
            if (kma == NULL) {
               send_error_msg();
            } else {
               hex2mem(obuf, kma, len);
               send_okay_msg();
            }
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
            if (sscanf(packet, "%" PRIx32 ",%" PRIx64 ",%zx", &type, &addr,
                       &len) != 3) {
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
            goto done;       // not reachable
         }
         case 'D': {
            warnx("Debugger detached");
            send_okay_msg();
            km_gdb_disable();
            goto done;
         }
#if 0
        case 'q': {
           /* TBD: separate command parsing into a sane table with "command" -> function */
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
      }       // switch
   }          // while
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
 * It is expected to be called ONLY when gdb is enabled and ONLY when the vcpu in question has exited the run.
 * If the exit reason is of interest to gdb, the function lets gdb try to handle it, and returns
 * 'true' for handled or 'false' for not handled.
 * If the exit reason is of no interest to gdb, the function  simply returns 'false' and lets
 * vcpu loop to handle the exit as it sees fit.
 *
 * Note that we map VM exit reason to a GDB 'signal', which is what needs to be communicated  back to gdb client.
 */
static bool km_gdb_handle_kvm_exit(km_vcpu_t *vcpu)
{
    int signum = 0;

    assert(km_gdb_enabled());
    signum = km_gdb_exit_reason_to_signal(vcpu);

    switch(signum) {
    case SIGINT:
    case SIGQUIT:
    case SIGKILL:
    case SIGTRAP:
    case SIGSEGV:
        gdb_handle_payload_stop(vcpu, signum);
        return true;

    case SIGTERM:
        // let GDB know we are done, and let vcpu loop to do it's completion
        send_response('W', 0, true);
        break;

    default:
        /* Handle this exit in the vcpu loop */
        break;
    }

    return false;
}


/*
 * Gdb stub thread start. Gets an array of vcpu pointers.
 */
static void *km_gdb_thread_entry(void *arg)
{
   int sig_fd;

   assert(km_gdb_enabled());
   // TBD: we assume single VCPU thread here, need to handle multi vCPUs
   km_vcpu_t *vcpu = ((km_vcpu_t **)arg)[0];

   // Get an fd so we can poll on it
   sig_fd = km_get_signalfd(GDBSTUB_SIGNAL);
   assert(sig_fd > 0);
   // Talk to GDB first time, before any vCPU run
   gdb_handle_payload_stop(vcpu, GDB_SIGFIRST);
   // talk to GDB, managing vcpu runs
   while(km_gdb_enabled()) {
    km_vcpu_t* msg;
    int ret;

    // Unblock vcpu_thread by sending any number to the channel
    chan_send_int(g_km_threads.vcpu_chan[0], 1);
    // Poll for either interrupt from gdb client, or for vcpu run completion
    km_gdb_poll_for_client_intr(vcpu, sig_fd);
    // get vcpu from vcpu thread - also sync the threads
    chan_recv(g_km_threads.gdb_chan, (void*)&msg); // should get vcpu
    assert(vcpu == msg);
    km_infox("%s: poll and vcpu_run done , got vcpu* (%p)", __FUNCTION__, msg);
    ret = km_gdb_handle_kvm_exit(vcpu);       // give control to gdb
    km_infox("Sending exit_handled (%d) to vcpu", ret);
    chan_send_int(g_km_threads.vcpu_chan[0], ret);
   }
   return (void *)NULL;
}

/*
 * Start gdb stub - wait for GDB to connect and start a thread to manage interaction with gdb client.
 */
void km_gdb_start_stub(int port, char *const payload_file)
{
    int i;

    assert(km_gdb_enabled());
    // First create all channels so the threads can communicate right away
    g_km_threads.gdb_chan = chan_init(0);
    if (g_km_threads.gdb_chan == NULL) {
       err(1, "Failed to create comm channel to talk to GDB threads");
   }
   for (i = 0; i < VCPU_THREAD_CNT; i++) {
      g_km_threads.vcpu_chan[i] = chan_init(0);
      if (g_km_threads.vcpu_chan[i] == NULL) {
         err(1, "Failed to create comm channel to talk to vCPU threads");
      }
   }
   km_infox("Enabling gdbserver on port %d...", port);
   if( gdb_wait_for_connect(payload_file) == -1) {
       errx(1, "Failed to connect to GDB");
   }

   // TBD: think about 'attach' on signal - dynamically starting this thread
   if (pthread_create(&g_km_threads.gdbsrv_thread, NULL, &km_gdb_thread_entry,
                      (void *)g_km_threads.vcpu) != 0) {
      err(1, "Failed to create GDB server thread ");
   }
}

/* closes the gdb socket and set port to 0 */
void km_gdb_disable(void)
{
    if (socket_fd > 0) {
       close(socket_fd);
        socket_fd = -1;
    }
    warnx("Disabling gdb");
    g_gdb_port = 0;
}

/* Called on vcpu thread and waits until GDB allows the next vcpu run */
void km_gdb_prepare_for_run(km_vcpu_t *vcpu)
{
   int buf;

   assert(km_gdb_enabled());
   // wait for gbd loop to allow vcpu to run
   chan_recv_int(g_km_threads.vcpu_chan[0], &buf);
   km_infox("%s: vcpu_run unblocked (got %d from gdb)", __FUNCTION__, buf);
}

/*
 * Called on vcpu thread, communicates to gdb to let it handle the exit
 * Returns "1" if gdb handled the exit and kvm loop does not need to,
 * 0 otherwise
 */
int km_gdb_ask_stub_to_handle_kvm_exit(km_vcpu_t *vcpu, int run_errno)
{
   int ret;

   assert(km_gdb_enabled());
   if ( km_gdb_exit_reason_to_signal(vcpu) == GDB_SIGNONE) {
       km_infox("gdb -not mine, keep going (reason: %d)", vcpu->cpu_run->exit_reason);
       return 0; // GDB can'thandle this exit
   }
   km_reset_pending_signal(GDBSTUB_SIGNAL);
   // Inform gdb thread that vcpu run is over
   km_signal_gdb_about_kvm_exit();
   km_infox("%s: KVM_RUN done.  Sending send(vcpu)->gdb_chan from thr 0x%lx",
            __FUNCTION__, pthread_self());
    // This will start stub interuction with GDB client:
   chan_send(g_km_threads.gdb_chan, (void *)vcpu);
   km_infox("%s: vcpu sent. Waiting for 1/0 from gdb thread", __FUNCTION__);
   // This will be unblocked when gdb stub wants vcpu cycle to resume:
   chan_recv_int(g_km_threads.vcpu_chan[0], &ret);
   km_infox("gdb handle_exception returned ret=%d to vcpu thread", ret);
   return ret;
}