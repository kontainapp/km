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

#define GDB_DEFAULT_PORT 3333

km_info_trace_t km_info_trace;

static inline void usage()
{
   errx(1,
        "Usage: km [-V[tag]] [-w] [-g[port]] <payload-file> [<payload args>]\n"
        "Options:\n"
        "\t-V[tag]  - Verbose print of internal messaging with matching tag\n"
        "\t-w       - Wait for SIGUSR1 before running VM payload\n"
        "\t-g[port] - Listens for gbd on <port> (default 3333) before running payload");
}

int main(int argc, char* const argv[])
{
   int opt;
   char* payload_file = NULL;
   bool wait_for_signal = false;
   int port;
   km_vcpu_t* vcpu;
   int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);

   while ((opt = getopt(argc, argv, "+wg::V::v")) != -1) {
      switch (opt) {
         case 'w':
            wait_for_signal = true;
            break;
         case 'g':
            if (optarg == NULL) {
               port = GDB_DEFAULT_PORT;
            } else if ((port = atoi(optarg)) == 0) {
               warnx("Wrong gdb port number");
               usage();
            }
            km_gdb_port_set(port);
            break;
         case 'V':
            if (optarg == NULL) {
               regcomp(&km_info_trace.tags, ".*", regex_flags);
            } else if (regcomp(&km_info_trace.tags, optarg, regex_flags) != 0) {
               warnx("Failed to compile -V regexp");
               usage();
            }
            km_info_trace.level = KM_TRACE_INFO;
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
   if (km_gdb_is_enabled() == 1) {
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
   if (km_gdb_is_enabled() == 1) {
      km_gdb_join_stub();
   }
   km_machine_fini();
   exit(machine.ret);
}
