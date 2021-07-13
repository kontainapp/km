/*
 * Copyright 2021 Kontain Inc
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

/*
 * Definitions related to code that is part of km but is mapped into the guest's
 * address space.
 */

#ifndef __KM_GUEST_H__
#define __KM_GUEST_H__

#include "km_mem.h"

#define CACHE_LINE_LENGTH 64 // bytes
#define BYTES_PER_POINTER 8

// Changes in this macro should be reflected in the declaration of km_hcargs in km_guest_asmcode.s
#define HC_ARGS_INDEX(vcpu_id) ((vcpu_id) * (CACHE_LINE_LENGTH / BYTES_PER_POINTER))

/*
 * Definition of symbols defined in the .km_guest_{test,data} sections.
 *
 * km_guest_{start,end} are defined in the km_guest_ldcmd script.
 * They represent the beginning and the end of the .km_guest_* sections combined.
 * We should only be taking the address of these symbols in km.
 */

extern uint8_t km_guest_start;
extern uint8_t km_guest_text_start;
extern uint8_t km_guest_data_start;
extern void* __km_interrupt_table[];
extern uint8_t km_guest_data_rw_start;
extern km_hc_args_t* km_hcargs[HC_ARGS_INDEX(KVM_MAX_VCPUS)];
extern uint8_t __km_handle_interrupt;
extern uint8_t __km_syscall_handler;
extern uint8_t __km_sigreturn;
extern uint8_t km_guest_end;

/*
 * Compute the guest virtual address of the km addresses that live
 * in km_guest*.[cs]
 */
static inline km_gva_t km_guest_kma_to_gva(km_kma_t km_guest_addr)
{
   km_gva_t gva;
   assert((uint64_t)km_guest_addr >= (uint64_t)&km_guest_start &&
          (uint64_t)km_guest_addr < (uint64_t)&km_guest_end);
   gva = GUEST_KMGUESTMEM_BASE_VA + ((uint64_t)km_guest_addr - (uint64_t)&km_guest_start);
   return gva;
}

#endif /* !defined(__KM_GUEST_H__) */
