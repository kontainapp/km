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

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"

int g_km_info_verbose;   // 0 is silent

static inline void usage()
{
   errx(1,
        "Usage: km [-V] [-w] [-g port] <payload-file> [<payload args>]\n"
        "Options:\n"
        "\t-V      - turn on Verbose printing of internal trace messages\n"
        "\t-w      - wait for SIGUSR1 before running VM payload\n"
        "\t-g port - listens for gbd to connect on <port> before running VM payload");
}

int main(int argc, char* const argv[])
{
   int opt;
   char* payload_file = NULL;
   bool wait_for_signal = false;
   km_vcpu_t* vcpu;
   while ((opt = getopt(argc, argv, "+wg:V")) != -1) {
      switch (opt) {
         case 'w':
            wait_for_signal = true;
            break;
         case 'g':
            km_gdb_port_set(atoi(optarg));
            if (!km_gdb_port_get()) {
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
   if (optind == argc) {
      usage();
   }
   payload_file = argv[optind];

   km_hcalls_init();
   km_machine_init();
   load_elf(payload_file);
   if ((vcpu = km_vcpu_get()) == NULL) {
      err(1, "failed to get main vcpu");
   }
   km_init_libc_main(vcpu, argc - optind, argv + optind);

   if (km_vcpu_set_to_run(vcpu, argc - optind) != 0) {
      err(1, "failed to set main vcpu to run");
   }
   if (km_gdb_is_enabled()) {
      km_gdb_start_stub(payload_file);
   }
   if (wait_for_signal) {
      warnx("Waiting for kill -SIGUSR1 `pidof km`");
      km_wait_for_signal(SIGUSR1);
   }
   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
   if (pthread_create(&km_main_vcpu()->vcpu_thread, NULL, km_vcpu_run_main, NULL) != 0) {
      err(2, "Failed to create main run_vcpu thread");
   }
   if (km_gdb_is_enabled()) {
      km_gdb_join_stub();
   }
   km_machine_fini();
   exit(machine.ret);
}
