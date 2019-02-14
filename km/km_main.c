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

#include <err.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan/chan.h"
#include "km.h"
#include "km_gdb.h"
#include "km_signal.h"

int g_km_info_verbose;   // 0 is silent

static inline void usage()
{
   errx(1,
        "Usage: km [-V] [-w] [-g port] <payload-file>"
        "\nOptions:"
        "\n\t-V      - turn on Verbose printing of internal trace messages"
        "\n\t-w      - wait for SIGUSR1 before running VM payload"
        "\n\t-g port - listens for gbd to connect on <port> before running VM payload");
}

int main(int argc, char* const argv[])
{
   int opt;
   char* payload_file = NULL;
   bool wait_for_signal = false;
   uint64_t fs;

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
         case 'V':
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
   load_elf(payload_file);
   fs = km_init_libc_main();
   km_hcalls_init();
   // Initialize main vcpu with payload entry point, main stack, and main pthread pointer
   km_vcpu_init(km_guest.km_ehdr.e_entry, GUEST_STACK_TOP - 1, fs);

   if (km_gdb_enabled()) {
      km_gdb_start_stub(g_gdb_port, payload_file);
   }

   if (wait_for_signal) {
      warnx("Waiting for kill -SIGUSR1 `pidof km`");
      km_wait_for_signal(SIGUSR1);
   }

   if (pthread_create(&km_main_vcpu()->vcpu_thread, NULL, km_vcpu_run_main, NULL) != 0) {
      err(2, "Failed to create main run_vcpu thread");
   }
   if (km_gdb_enabled()) {
      km_gdb_stop_stub();
   }
   km_machine_fini();
   exit(0);
}
