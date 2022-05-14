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

#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "km.h"
#include "km_mem.h"

/*
 * This function handles finding all of the link_map entries for the current payload.
 * And then calling a caller supplied function to visit each entry in the list.
 * The dynamic linker manages this list and the list resides in the guest.  So we need to
 * use distasteful methods to find the list and visit each entry.  We know that dlopen()
 * accesses the head of the link_map list so we pickup the address of the list head
 * from the dlopen() code.  The following disassembly of dlopen() shows the line of
 * code that contains the instruction pointer relative address of the head of the link_map
 * list. The line "mov 0x0(%rip),%rax" is the one we want.
0000000000000000 <dlopen>:
   0:   41 57                   push   %r15
   2:   41 56                   push   %r14
   4:   41 55                   push   %r13
   6:   41 54                   push   %r12
   8:   55                      push   %rbp
   9:   53                      push   %rbx
   a:   48 81 ec c8 01 00 00    sub    $0x1c8,%rsp
* 11:   48 8b 05 00 00 00 00    mov    0x0(%rip),%rax        # 18 <dlopen+0x18>
  18:   48 89 7c 24 28          mov    %rdi,0x28(%rsp)
  1d:   89 74 24 1c             mov    %esi,0x1c(%rsp)
  21:   48 c7 44 24 70 00 00    movq   $0x0,0x70(%rsp)
  28:   00 00
  2a:   48 85 ff                test   %rdi,%rdi
 * We extract the offset from that instruction and then compute the address of the
 * head of the list.  With that pointer we can walk the list of link_map's.
 *
 * With gcc-12 the assembly is a bit different but the instruction is question is the same, so we
 * check for the pattern in two locations.
 *
 * Assumptions:
 * - dlopen() is part of musl libc which also contains the dynamic linker.
 * - km_dynlinker.km_dlopen will contain the gva of dlopen().
 * - we don't need to walk this list while the guest is running since we are
 *   not holding a mutex to exclude changes to the list.
 */
int km_link_map_walk(link_map_visit_function_t* callme, void* visitargp)
{
   link_map_t* lmp_kma;
   link_map_t* lmp_gva;
   km_kma_t dlopen_kma;
   uint32_t rip_offset_of_head;
   km_kma_t adderss_of_linkmaphead_kma;
   km_gva_t linkmapheadp_gva;
   int rc = 0;

   static const uint64_t KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR = 0x11;
   static const uint64_t KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR_gcc_12 = 0x14;
   static const uint64_t KM_DLOPEN_LOAD_HEAD_INSTR_LEN = 0x7;

   uint64_t offset_to_load_head_instr;

   // Find address of head of the link_map list.
   dlopen_kma = km_gva_to_kma(km_guest.km_dlopen);
   km_infox(KM_TRACE_KVM, "value of symbol dlopen is 0x%lx, kma %p", km_guest.km_dlopen, dlopen_kma);
   if (km_guest.km_dlopen == 0) {
      km_infox(KM_TRACE_KVM, "Address of dlopen not found.  Not dynamically linked?");
      return 0;
   }
   if (*((uint8_t*)dlopen_kma + KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR) == 0x48 &&
       *((uint8_t*)dlopen_kma + KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR + 1) == 0x8b) {
      offset_to_load_head_instr = KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR;
   } else if (*((uint8_t*)dlopen_kma + KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR_gcc_12) == 0x48 &&
              *((uint8_t*)dlopen_kma + KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR_gcc_12 + 1) == 0x8b) {
      offset_to_load_head_instr = KM_DLOPEN_OFFSET_TO_LOAD_HEAD_INSTR_gcc_12;
   } else {
      km_infox(KM_TRACE_KVM, "Unexpected instruction in dlopen, has musl dlopen() changed?");
      return rc;
   }

   // Fetch offset relative to the rip.
   rip_offset_of_head = *(uint32_t*)((char*)dlopen_kma + offset_to_load_head_instr + 3);
   adderss_of_linkmaphead_kma = (km_kma_t)((uint64_t)dlopen_kma + offset_to_load_head_instr +
                                           KM_DLOPEN_LOAD_HEAD_INSTR_LEN + rip_offset_of_head);
   km_infox(KM_TRACE_KVM,
            "addr of link map head: gva 0x%lx, kma %p",
            km_guest.km_dlopen + offset_to_load_head_instr + KM_DLOPEN_LOAD_HEAD_INSTR_LEN +
                rip_offset_of_head,
            adderss_of_linkmaphead_kma);
   linkmapheadp_gva = *(uint64_t*)adderss_of_linkmaphead_kma;
   km_infox(KM_TRACE_KVM, "link map head gva 0x%lx", linkmapheadp_gva);

   // Visit the entries in the link_map list
   lmp_gva = (link_map_t*)linkmapheadp_gva;
   while (lmp_gva != NULL) {
      km_infox(KM_TRACE_KVM, "Examining link map entry at %p", lmp_gva);
      lmp_kma = (link_map_t*)km_gva_to_kma((km_gva_t)lmp_gva);
      rc = (*callme)(lmp_kma, lmp_gva, visitargp);
      if (rc != 0) {
         break;
      }
      lmp_gva = lmp_kma->l_next;
   }
   return rc;
}
