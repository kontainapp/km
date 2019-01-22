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
#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "km.h"


static inline void usage()
{
   errx(1, "Usage: km [-w] <payload-file>\n"
            "Options:\n\t-w - create a VM with payload and wait for SIGUSR1 before running it");
}

int main(int argc, char *const argv[])
{
   km_vcpu_t *vcpu;
   uint64_t guest_entry, end;
   int opt;
   char *payload_file = NULL;
   bool wait_for_signal = false;

   while ((opt = getopt(argc, argv, "w")) != -1) {
      switch (opt) {
         case 'w':
            wait_for_signal = true;
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
   load_elf(payload_file, (void *)km_gva_to_kma(0), &guest_entry, &end);
   km_mem_brk(end);
   vcpu = km_vcpu_init(guest_entry, GUEST_STACK_TOP - 1);

   /*
    * If requested, wait for SIGUSR1 before running CPU. It is needed in
    * Docker/OCI to easily support 'runc create' + 'runc delete' as a separate
    * steps. On create, do 'km -w <payload>' , and on 'start' simply signal
    * SIGUSR1 to the km process.
    */
   if (wait_for_signal) {
      sigset_t signal_set;
      int received_signal;

      sigemptyset(&signal_set);
      sigaddset(&signal_set, SIGUSR1);
      sigprocmask(SIG_BLOCK, &signal_set, NULL);
      sigwait(&signal_set, &received_signal);
  }

  /*
   * Run the first vcpu
   */
   km_vcpu_run(vcpu);
}
