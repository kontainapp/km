/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <stdio.h>
#include <err.h>
#include <stdint.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "km.h"
#include "km_gdb.h"
#include "km_thread.h"
#include "km_signal.h"
#include "chan/chan.h"

km_threads_t g_km_threads;
int g_km_info_verbose; // 0 is silent

static inline void usage()
{
   errx(1, "Usage: km [-V] [-w] [-g port] <payload-file>"
            "\nOptions:"
            "\n\t-V      - turn on Verbose printing of internal trace messages"
            "\n\t-w      - wait for SIGUSR1 before running VM payload"
            "\n\t-g port - listens for gbd to connect on <port> before running VM payload");
}

static void *km_start_vcpu_thread(void *arg)
{
   km_vcpu_t *vcpu = (km_vcpu_t *)arg;

   // unblock signal in the VM (and block on the current thead)
   km_vcpu_unblock_signal(vcpu, GDBSTUB_SIGNAL);
   // and now go into the run loop
   km_vcpu_run(vcpu);
   return (void *)NULL;
}

int main(int argc, char *const argv[])
{
   uint64_t guest_entry;
   int i;
   int opt;
   char *payload_file = NULL;
   bool wait_for_signal = false;

   while ((opt = getopt(argc, argv, "wg:V")) != -1) {
      switch (opt) {
         case 'w':
            wait_for_signal = true;
            break;
         case 'g':
            g_gdb_port = atoi(optarg);
            if (!g_gdb_port) {
               usage();
            }
            break;
         case  'V':
            g_km_info_verbose = 1;
            break;
         case '?':
         default:
            usage();
      }
   }
   if (optind != argc - 1) {
      usage();
   }
   payload_file = argv[optind];

   km_machine_init();
   load_elf(payload_file, &guest_entry);
   km_hcalls_init();
   for (i=0; i< VCPU_THREAD_CNT; i++) {
      g_km_threads.vcpu[i] = km_vcpu_init(guest_entry, GUEST_STACK_TOP - 1);
   }

   if (km_gdb_enabled()) {
      km_gdb_start_stub(g_gdb_port, payload_file);
   }

   if (wait_for_signal) {
      warnx("Waiting for kill -SIGUSR1 `pidof km`");
      km_wait_for_signal(SIGUSR1);
   }

   g_km_threads.main_thread = pthread_self();
   for (i = 0; i < VCPU_THREAD_CNT; i++) {
      if (pthread_create(&g_km_threads.vcpu_thread[i], NULL,
                         &km_start_vcpu_thread,
                         (void *)g_km_threads.vcpu[i]) != 0) {
         err(2, "Failed to create run_vcpu thread %d", i);
      }
   }
   if (km_gdb_enabled()) {
      pthread_join(g_km_threads.gdbsrv_thread, NULL);
      chan_dispose(g_km_threads.gdb_chan);
   }
   for (i = 0; i < VCPU_THREAD_CNT; i++) {
      pthread_join(g_km_threads.vcpu_thread[i], NULL);
      if (g_km_threads.vcpu_chan[i])
         chan_dispose(g_km_threads.vcpu_chan[i]);
   }
   return 1;
}
