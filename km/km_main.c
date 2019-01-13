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

#include <string.h>

#include "km.h"

int main(int argc, char const *argv[])
{
   km_vcpu_t *vcpu;
   uint64_t guest_entry, end;

   if (argc != 2) {
      err(1, "Usage: km exec_file");
   }
   km_machine_init();
   load_elf(argv[1], (void *)km_gva_to_kma(0), &guest_entry, &end);
   km_mem_brk(end);
   vcpu = km_vcpu_init(guest_entry, GUEST_STACK_TOP - 1);
   /*
    * Run the first vcpu
    */
   km_vcpu_run(vcpu);
}
