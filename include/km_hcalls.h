/*
 * Copyright 2021 Kontain Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
   __asm__ __volatile__("mov %0,%%gs:0;"
                        "outl %1, %2"
                        :
                        : "r"(arg), "a"(0), "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
}

typedef enum {
   HC_CONTINUE = 0,
   HC_STOP,
   HC_ALLSTOP,
   HC_DOFORK,
} km_hc_ret_t;

typedef km_hc_ret_t (*km_hcall_fn_t)(void* vcpu,
                                     int hc __attribute__((__unused__)),
                                     km_hc_args_t* guest_addr);
typedef struct {
   uint64_t count;   // times called
   uint64_t total;   // usecs in hypercall
   uint64_t min;     // min usecs
   uint64_t max;     // max usecs
} km_hc_stats_t;

extern const km_hcall_fn_t km_hcalls_table[];
extern km_hc_stats_t* km_hcalls_stats;

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
   HC_reserved3 = KM_MAX_HCALL - 5,
   HC_reserved4 = KM_MAX_HCALL - 4,
   HC_unmapself = KM_MAX_HCALL - 6,
   HC_snapshot = KM_MAX_HCALL - 7,
   HC_snapshot_getdata = KM_MAX_HCALL - 8,
   HC_snapshot_putdata = KM_MAX_HCALL - 9,
   HC_start = KM_MAX_HCALL - 10,   // must be last in list
};

extern const char* const km_hc_name_get(int hc);

#define KM_TRACE_HC "hypercall"
#define KM_TRACE_SCHED "sched"

#endif /* #ifndef __KM_HCALLS_H__ */
