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

#ifndef KM_DLLINKAGE_H_
#define KM_DLLINKAGE_H_

typedef struct km_dlsymbol {
    char *sym_name;
    void *sym_addr;
} km_dlsymbol_t;

typedef struct km_dlentry {
    char *dlname;
    km_dlsymbol_t *symbols;
    int nsymbols;
} km_dlentry_t;
#endif