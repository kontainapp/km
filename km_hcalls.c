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
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "km_hcalls.h"
#include "km.h"

/*
 * User space (km) implementation of hypercalls.
 * These functions are called from kvm_vcpu_run() when guest makes hypercall
 * vmexit.
 *
 * km_hcalls_init() registers hypercalls in the table indexed by hcall #
 * TODO: make registration configurable so only payload specific set of hcalls
 * is registered
 */

/*
 * guest code executed exit(status);
 */
static int halt_hcall(void *ga, int *status)
{
   km_hlt_hc_t *arg = (typeof(arg))ga;
   *status = arg->exit_code;
   return 1;
}

/*
 * read/write
 */
static int rw_hcall(void *ga, int *status)
{
   km_rw_hc_t *arg = (typeof(arg))ga;

   if (arg->r_w == READ) {
      arg->hc_ret = read(arg->fd, km_gva_to_kma(arg->data), arg->length);
   } else {
      arg->hc_ret = write(arg->fd, km_gva_to_kma(arg->data), arg->length);
   }
   arg->hc_errno = errno;
   return 0;
}

static int accept_hcall(void *ga, int *status)
{
   km_accept_hc_t *arg = (typeof(arg))ga;

   arg->hc_ret = accept(arg->sockfd, km_gva_to_kma(arg->addr), &arg->addrlen);
   arg->hc_errno = errno;
   return 0;
}

static int bind_hcall(void *ga, int *status)
{
   km_bind_hc_t *arg = (typeof(arg))ga;

   arg->hc_ret = bind(arg->sockfd, km_gva_to_kma(arg->addr), arg->addrlen);
   arg->hc_errno = errno;
   return 0;
}

static int listen_hcall(void *ga, int *status)
{
   km_listen_hc_t *arg = (typeof(arg))ga;

   arg->hc_ret = listen(arg->sockfd, arg->backlog);
   arg->hc_errno = errno;
   return 0;
}

static int socket_hcall(void *ga, int *status)
{
   km_socket_hc_t *arg = (typeof(arg))ga;

   arg->hc_ret = socket(arg->domain, arg->type, arg->protocol);
   arg->hc_errno = errno;
   return 0;
}

km_hcall_fn_t km_hcalls_table[KM_HC_COUNT] = {
    halt_hcall,
    rw_hcall,
    accept_hcall,
    bind_hcall,
    listen_hcall,
    socket_hcall,
};
