/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

/*
 * Definitions related to code that is part of km but is mapped into the guest's
 * address space.
 */

#ifndef __KM_GUEST_H__
#define __KM_GUEST_H__

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
   km_infox(KM_TRACE_MEM, "guest kma %p -> gva 0x%lx", km_guest_addr, gva);
   return gva;
}

#endif /* !defined(__KM_GUEST_H__) */
