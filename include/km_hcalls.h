/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#ifndef __KM_HCALLS_H__
#define __KM_HCALLS_H__

#include <stdint.h>

/*
 * Definitions of hypercalls guest code (payload) can make into the KontainVM.
 * This file is to be included in both KM code and the guest library code.
 */
static const int KM_HCALL_PORT_BASE = 0x8000;

typedef struct km_hc_args {
   uint64_t hc_ret;
   uint64_t arg1;
   uint64_t arg2;
   uint64_t arg3;
   uint64_t arg4;
   uint64_t arg5;
   uint64_t arg6;
} km_hc_args_t;

static inline void km_hcall(int n, km_hc_args_t* arg)
{
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)arg)), "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
}

typedef enum {
   HC_CONTINUE = 0,
   HC_STOP,
   HC_ALLSTOP,
} km_hc_ret_t;

typedef km_hc_ret_t (*km_hcall_fn_t)(void* vcpu,
                                     int hc __attribute__((__unused__)),
                                     km_hc_args_t* guest_addr);

extern km_hcall_fn_t km_hcalls_table[];

/*
 * Maximum hypercall number, defines the size of the km_hcalls_table
 */
#define KM_MAX_HCALL 512

/*
 * Hypercalls that don't translate directly into system calls.
 */
enum km_internal_hypercalls {
   HC_reserved1 = KM_MAX_HCALL - 1,
   HC_reserved2 = KM_MAX_HCALL - 2,
   HC_guest_interrupt = KM_MAX_HCALL - 3,
   HC_km_unittest = KM_MAX_HCALL - 4,
   HC_procfdname = KM_MAX_HCALL - 5,
   HC_unmapself = KM_MAX_HCALL - 6,
};

extern const char* const km_hc_name_get(int hc);

#define KM_TRACE_HC "hypercall"

#endif /* #ifndef __KM_HCALLS_H__ */
