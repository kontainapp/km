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
 */

#include <signal.h>
#include <string.h>

#include "km_dll.h"
#include "km_elf.h"
#include "km_mem.h"
#include "km_signal.h"

void km_dl_init()
{
    if (km_guest.km_dllist == 0 || km_guest.km_ndllist == 0) {
        return;
    }
    void *kmaddr = km_gva_to_kma(km_guest.km_dllist);
    if (kmaddr == 0) {
        errx(1, "Bad km_dllist_address: 0x%lx", km_guest.km_dllist);
    }
    machine.dls.dllist = kmaddr;
    if ((kmaddr = km_gva_to_kma(km_guest.km_ndllist)) == 0) {
        errx(1, "Bad km_ndllist_address: 0x%lx", km_guest.km_dllist);
    }
    machine.dls.ndllist = *((int *) kmaddr);
}

void km_dl_fini()
{}

static char *dl_errmsg = NULL;
static void km_set_dlerror(km_vcpu_t *vcpu, char *msg)
{
    dl_errmsg = msg;
}

uint64_t km_dlerror(km_vcpu_t *vcpu, char *msg, size_t length)
{
    if (dl_errmsg == NULL) {
        return 0;
    }
    strncpy(msg, dl_errmsg, length);
    dl_errmsg = NULL;
    return 1;

}

uint64_t km_dlopen(km_vcpu_t *vcpu, char *pathname, int flags)
{
    if (machine.dls.ndllist == 0) {
        return 0;
    }

    assert(machine.dls.dllist != NULL);
    for (int i = 0; i < machine.dls.ndllist; i++) {
        char *dlname = km_gva_to_kma((uint64_t)machine.dls.dllist[i].dlname);
        if (dlname == NULL) {
            km_set_dlerror(vcpu, "dlopen: bad internal table (dllist.dlname)");
            return 0;
        }
        if (strcmp(pathname, dlname) == 0) {
            return i + 1;
        }
    }
    km_set_dlerror(vcpu, "dlopen: path does not exist");
    return 0;
}

uint64_t km_dlsym(km_vcpu_t *vcpu, uint64_t handle, char *sym)
{
    if (handle == 0 || handle > machine.dls.ndllist) {
        km_set_dlerror(vcpu, "dlsym: bad handle");
        return 0;
    }

    km_dlentry_t *ent = &machine.dls.dllist[handle - 1];
    km_dlsymbol_t *symbols = km_gva_to_kma((uint64_t)ent->symbols);
    if (symbols == NULL) {
        km_set_dlerror(vcpu, "dlsym: symbol root");
        return 0;
    }

    for (int i = 0; i < ent->nsymbols; i++) {
        char *sym_name = km_gva_to_kma((uint64_t)symbols[i].sym_name);
        if (strcmp(sym_name, sym) == 0) {
            return (uint64_t)symbols[i].sym_addr;
        }
    }
    km_set_dlerror(vcpu, "dlopen: symbol does not exist");
    return 0;
}