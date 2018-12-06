/*
 * TODO: Header
 */
#include <stdio.h>
#include <err.h>
#include <stdint.h>

#include <string.h>

#include "km.h"

int main(int argc, char const *argv[])
{
   km_vcpu_t *vcpu;
   uint64_t guest_entry;

    if (argc != 2) {
       err(1, "Usage: km exec_file");
    }
    km_machine_init();
    load_elf(argv[1], km_gva_to_kma(0), &guest_entry);
    vcpu = km_vcpu_init(guest_entry, km_guest_memsize());
    /*
     * Run the first vcpu
     */
    km_vcpu_run(vcpu);
}
