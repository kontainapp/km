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

#include <stdint.h>

/*
 * Definitions of hypercalls guest code (payload) can make into the KontainVM.
 * This file is to be included in both KM code and the guest library code.
 */
static const int KM_HCALL_PORT_BASE = 0x8000;

typedef enum km_hcall {
   KM_HC_BASE = 0,
   KM_HC_HLT = KM_HC_BASE,
   KM_HC_RW,
   KM_HC_ACCEPT,
   KM_HC_BIND,
   KM_HC_LISTEN,
   KM_HC_SOCKET,
   KM_HC_SOCKOPT,
   KM_HC_COUNT
} km_hcall_t;

typedef enum { READ, WRITE } km_rwdir_t;

typedef struct {
   int hc_ret;
   int hc_errno;
} km_common_hc_t;
#define km_hc_cmn_t km_common_hc_t cmn
#define hc_errno cmn.hc_errno
#define hc_ret cmn.hc_ret

typedef struct km_hlt_hc {
   int exit_code;
} km_hlt_hc_t;

typedef struct km_rw_hc {
   km_hc_cmn_t;
   int fd;
   int r_w;
   uint64_t data;
   uint32_t length;
} km_rw_hc_t;

typedef struct km_accept_hc {
   km_hc_cmn_t;
   int sockfd;
   uint64_t addr;       // struct sockaddr
   uint32_t addrlen;
} km_accept_hc_t;

typedef struct km_bind_hc {
   km_hc_cmn_t;
   int sockfd;
   uint64_t addr;       // struct sockaddr
   uint32_t addrlen;
} km_bind_hc_t;

typedef struct km_listen_hc {
   km_hc_cmn_t;
   int sockfd;
   int backlog;
} km_listen_hc_t;

typedef struct km_socket_hc {
   km_hc_cmn_t;
   int domain;
   int type;
   int protocol;
} km_socket_hc_t;

enum { GET, SET } km_getset_sockopt_t;

typedef struct km_sockopt_hc {
   km_hc_cmn_t;
   int sockfd;
   int get_set;
   int level;
   int optname;
   uint64_t optval;
   uint32_t optlen;
} km_sockopt_hc_t;
