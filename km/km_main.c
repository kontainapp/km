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
        "\t-P<bits> - Set guest physical memory bus size in bits.\n"
        "\t           i.e. 32 means 4GiB, 33 8GiB, 34 16GiB, etc. (Override auto detection)\n"
        "\t-w       - Wait for SIGUSR1 before running VM payload\n"
        "\t-g[port] - Listen for gbd on <port> (default 3333) before running payload\n"
        "\t-1       - force using of 1G pages in payload (assumes hardware support)");
}

int main(int argc, char* const argv[])
{
   int opt;
   int port;
   km_vcpu_t* vcpu;
   km_machine_init_params_t km_machine_init_params = {};
   char* payload_file = NULL;
   bool wait_for_signal = false;
   int gpbits = 0;   // Width of guest physical memory bus.
   char* ep = NULL;
   int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);

   while ((opt = getopt(argc, argv, "+w1g::V::P:")) != -1) {
      switch (opt) {
         case 'w':
            wait_for_signal = true;
            break;
         case '1':
            km_machine_init_params.force_pdpe1g = 1;
            break;
         case 'g':
            if (optarg == NULL) {
               port = GDB_DEFAULT_PORT;
            } else if ((port = atoi(optarg)) == 0) {
               warnx("Wrong gdb port number '%s'", optarg);
               usage();
            }
            km_gdb_port_set(port);
            break;
         case 'P':
            gpbits = strtol(optarg, &ep, 0);
            if (ep == NULL || *ep != '\0') {
               warnx("Wrong memory bus size '%s'", optarg);
               usage();
            }
            if (gpbits < 32 || gpbits >= 63) {
               warnx("Guest memory bus width must be between 32 and 63 - got '%d'", gpbits);
               usage();
            }
            km_machine_init_params.guest_physmem = 1UL << gpbits;
            if (km_machine_init_params.guest_physmem > GUEST_MAX_PHYSMEM_SUPPORTED) {
               warnx("Guest physical memory must be < 0x%lx - got 0x%lx (bus width %d)",
                     GUEST_MAX_PHYSMEM_SUPPORTED,
                     km_machine_init_params.guest_physmem,
                     gpbits);
               usage();
            }
            break;
         case 'V':
            if (optarg == NULL) {
               regcomp(&km_info_trace.tags, ".*", regex_flags);
            } else if (regcomp(&km_info_trace.tags, optarg, regex_flags) != 0) {
               warnx("Failed to compile -V regexp '%s'", optarg);
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
   km_machine_init(&km_machine_init_params);
   load_elf(payload_file);
   if ((vcpu = km_vcpu_get()) == NULL) {
      err(1, "Failed to get main vcpu");
   }
   km_init_libc_main(vcpu, argc - optind, argv + optind);
   if (km_vcpu_set_to_run(vcpu, argc - optind) != 0) {
      err(1, "failed to set main vcpu to run");
   }
   if (wait_for_signal) {
      warnx("Waiting for kill -SIGUSR1 `pidof km`");
      km_wait_for_signal(SIGUSR1);
   }
   if (km_gdb_is_enabled() == 1) {
      gdbstub.intr_eventfd = eventfd(0, 0);   // we will need it for km_vcpu_run_main and gdb main loop
   }
   __atomic_add_fetch(&machine.vm_vcpu_run_cnt, 1, __ATOMIC_SEQ_CST);   // vm_vcpu_run_cnt++
   vcpu = km_main_vcpu();
   if (pthread_create(&vcpu->vcpu_thread, NULL, km_vcpu_run_main, NULL) != 0) {
      err(2, "Failed to create main run_vcpu thread");
   }
   if (km_gdb_is_enabled() == 1) {   // TODO: think about 'attach' on signal
      km_infox(KM_TRACE_GDB, "Enabling gdbserver on port %d...", km_gdb_port_get());
      if (km_gdb_wait_for_connect(payload_file) == -1) {
         errx(1, "Failed to connect to gdb");
      }
      km_gdb_main_loop(vcpu);
      km_gdb_disable();
   }
   km_machine_fini();
   exit(machine.ret);
}
