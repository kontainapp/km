/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Implementation of X86 specific KVM calls between gdb server stub and KVM.
 */

/*
 * Based on Solo5 tenders/hvt/hvt_gdb_kvm_x86_64.c
 *
 * Copyright (c) 2015-2018 Contributors as noted in the AUTHORS file
 *
 * This file is part of Solo5, a sandboxed execution environment.
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <stdio.h>

#include "bsd_queue.h"

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"

typedef struct breakpoint_t {
   gdb_breakpoint_type_t type;
   km_gva_t addr;
   size_t len;
   uint32_t refcount;
   uint8_t saved_insn; /* for software breakpoints */

   SLIST_ENTRY(breakpoint_t) entries;
} breakpoint_t;

SLIST_HEAD(breakpoints_head, breakpoint_t);
static struct breakpoints_head sw_breakpoints;
static struct breakpoints_head hw_breakpoints;

/*
 * See Intel SDM Vol3, 17.2 "Debug Registers".
 */
#define MAX_HW_BREAKPOINTS 4   // DR7 has space for 4 breakpoints
#define DR dbg.arch.debugreg   // generic debug register
#define DR7 DR[7]              // debug control register

static uint32_t nr_hw_breakpoints = 0;

static const uint8_t int3 = 0xcc;   // trap instruction used for software breakpoints

/*
 * Read /proc/self/maps looking for the entry that covers the address
 * supplied by the addr argument.  When a matching range is found,
 * return the PROT_{READ,WRITE,EXEC} equivalent of the rwxp field in *protection
 * If no matching addr range is found or we couldn't open the maps
 * file return a negative value.
 * If a page protection was found return 0.
 */
#define PROC_SELF_MAPS "/proc/self/maps"
int km_get_page_protection(km_kma_t addr, int* protection)
{
   FILE* procmaps;
   char linebuffer[256];
   uint64_t start;
   uint64_t end;
   char prot[strlen("rwxp") + 4];
   int rv = -1;

   if ((procmaps = fopen(PROC_SELF_MAPS, "r")) == NULL) {
      km_info(KM_TRACE_MEM, "can't open %s for reading", PROC_SELF_MAPS);
      return -errno;
   }

   while (fgets(linebuffer, sizeof(linebuffer), procmaps) != NULL) {
      if (sscanf(linebuffer, "%lx-%lx %5s ", &start, &end, prot) == 3) {
         if ((uint64_t)addr >= start && (uint64_t)addr < end) {
            *protection = (prot[0] == 'r' ? PROT_READ : 0) |
                          (prot[1] == 'w' ? PROT_WRITE : 0) |
                          (prot[2] == 'x' ? PROT_EXEC : 0);
            rv = 0;
            break;
         }
      } else {
         km_infox(KM_TRACE_GDB, "Ignoring mangled line from %s: %s", PROC_SELF_MAPS, linebuffer);
      }
   }

   fclose(procmaps);
   return rv;
}

static int kvm_arch_insert_sw_breakpoint(struct breakpoint_t* bp)
{
   uint8_t* insn;   // existing instructions at the bp location
   void* pageaddr;

   if ((insn = (uint8_t*)km_gva_to_kma(bp->addr)) == NULL) {
      warn("failed to insert bp - address %lx is not in guest va space", bp->addr);
      return -1;
   }
   bp->saved_insn = *insn;
   pageaddr = (void *)((unsigned long)insn & KM_PAGE_MASK);

   // Make the page writeable if not already writeable.
   int prot;
   if ((km_get_page_protection(insn, &prot)) == -1) {
      km_err_msg(0, "Can't determine mem protection at gva 0x%lx", bp->addr);
      return -1;
   }
   if ((prot & PROT_EXEC) == 0) { // Do we care if they try to put breakpoints in non-executable pages?
      km_err_msg(0, "Putting breakpoint in non-executable page at gva %lx", bp->addr);
   }
   if ((prot & PROT_WRITE) == 0) {
      if (mprotect(pageaddr, KM_PAGE_SIZE, prot | PROT_WRITE) < 0) {
         km_err_msg(0, "Can't mark page at gva 0x%lx writeable", bp->addr);
         return -1;
      }
   }

   /*
    * The debugger keeps track of the length of the instruction. We only need to manipulate the
    * first byte.
    */
   *insn = int3;

   if ((prot & PROT_WRITE) == 0) {
      if (mprotect(pageaddr, KM_PAGE_SIZE, prot) < 0) {
         // On failure we leave the page writable for the duration of the payload.
         warn("Can't remove write protect from page 0x%lx, leaving it writable", bp->addr);
      }
   }
   return 0;
}

static int kvm_arch_remove_sw_breakpoint(struct breakpoint_t* bp)
{
   uint8_t* insn;
   void* pageaddr;

   if ((insn = (uint8_t*)km_gva_to_kma(bp->addr)) == NULL) {
      return -1;
   }
   pageaddr = (void *)((unsigned long)insn & KM_PAGE_MASK);

   int prot;
   if (km_get_page_protection(insn, &prot) == -1) {
      km_err_msg(0, "Can't determine mem protection at gva 0x%lx", bp->addr);
      return -1;
   }
   if ((prot & PROT_WRITE) == 0) {
      if (mprotect(pageaddr, KM_PAGE_SIZE, prot | PROT_WRITE) < 0) {
         km_err_msg(0, "Can't mark page at gva 0x%lx writeable", bp->addr);
         return -1;
      }
   }

   assert(*insn == int3);
   *insn = bp->saved_insn;

   if ((prot & PROT_WRITE) == 0) {
      if (mprotect(pageaddr, KM_PAGE_SIZE, prot) < 0) {
         km_err_msg(0, "Can't remove write protect from page at gva 0x%lx, leaving it writable", bp->addr);
      }
   }
   return 0;
}

// Sets VCPU Debug registers to match current breakpoint list.
int km_gdb_update_vcpu_debug(km_vcpu_t* vcpu, uint64_t unused)
{
   struct kvm_guest_debug dbg = {0};
   static const uint8_t bp_condition_code[] = {
       [GDB_BREAKPOINT_HW] = 0x0,       // Break on instruction execution only.
       [GDB_WATCHPOINT_WRITE] = 0x1,    // Break on data writes only.
       [GDB_WATCHPOINT_READ] = 0x2,     // Break on data reads only.
       [GDB_WATCHPOINT_ACCESS] = 0x3,   // Break on data reads or writes but not instruction fetches.
   };
   static const uint8_t mem_size_code[] = {
       [1] = 0x0,   // 00 — 1-byte length.
       [2] = 0x1,   // 01 — 2-byte length.
       [4] = 0x3,   // 10 — 8-byte length.
       [8] = 0x2,   // 11 — 4-byte length.
   };

   if (gdbstub.stepping) {
      dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
   }
   if (!SLIST_EMPTY(&sw_breakpoints)) {
      dbg.control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
   }

   int n = 0;
   if (!SLIST_EMPTY(&hw_breakpoints)) {
      struct breakpoint_t* bp;
      dbg.control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
      DR7 = (1 << 9 | 1 << 10);   // SDM recommendation for forward compatibility
      SLIST_FOREACH (bp, &hw_breakpoints, entries) {   // Configure HW breakpoints in debug registers
         DR[n] = bp->addr;
         DR7 |= (2 << (n * 2));                                       // global breakpoint
         DR7 |= (bp_condition_code[bp->type] << (16 + n * 4));        // read/write fields
         DR7 |= ((uint32_t)mem_size_code[bp->len] << (18 + n * 4));   // memory length
         vcpu->dr_regs[n] = bp->addr;
         km_infox(KM_TRACE_GDB,
                  "%s: hw breakpoint, n %d, type %d, addr 0x%lx, len %ld",
                  __FUNCTION__,
                  n,
                  bp->type,
                  bp->addr,
                  bp->len);
         n++;
      }
   }
   if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_GUEST_DEBUG, &dbg) == -1) {
      err(1, "KVM_SET_GUEST_DEBUG failed");
      return -1;   // Not reachable
   }
   return 0;
}

static int km_gdb_update_guest_debug(void)
{
   int count;
   if ((count = km_vcpu_apply_all(km_gdb_update_vcpu_debug, 0)) != 0) {
      warnx("Failed update guest debug info for %d VCPU(s)", count);
      return -1;
   }
   return 0;
}

static struct breakpoint_t* bp_list_find(gdb_breakpoint_type_t type, km_gva_t addr, size_t len)
{
   struct breakpoint_t* bp;

   switch (type) {
      case GDB_BREAKPOINT_SW:
         SLIST_FOREACH (bp, &sw_breakpoints, entries) {
            if (bp->addr == addr) {
               if (bp->len != len) {
                  warnx("SW breakpoint of different len at 0x%lx", addr);
               }
               return bp;
            }
         }
         break;

      case GDB_BREAKPOINT_HW:
      case GDB_WATCHPOINT_WRITE:
      case GDB_WATCHPOINT_READ:
      case GDB_WATCHPOINT_ACCESS:
         SLIST_FOREACH (bp, &hw_breakpoints, entries) {   // We only support hardware watchpoints.
            if (bp->addr == addr) {
               if (bp->len != len) {
                  warnx("HW breakpoint of different len at 0x%lx", addr);
               }
               return bp;
            }
         }
         break;

      default:
         assert(0);
   }

   return NULL;
}

/*
 * Find any breakpoint that covers the passed address.
 * Return the breakpoint_t entry pointer if a match is found.
 * Return NULL if no match is found.
 */
static struct breakpoint_t* bp_list_find_byaddr(km_gva_t addr)
{
   struct breakpoint_t* bp;

   // Software breakpoints first
   SLIST_FOREACH (bp, &sw_breakpoints, entries) {
      if (bp->addr == addr) {
         return bp;
      }
   }

   // Try hardware breakpoints next.
   SLIST_FOREACH (bp, &hw_breakpoints, entries) {   // We only support hardware watchpoints.
      if (addr >= bp->addr && addr < (bp->addr + bp->len)) {
         return bp;
      }
   }

   // Didn't find it.
   return NULL;
}

/*
 * Adds a new breakpoint to the list of breakpoints. Returns the found or
 * created breakpoint. Returns NULL in case of failure or if we reached the max
 * number of allowed hardware breakpoints (4).
 */
static struct breakpoint_t* bp_list_insert(gdb_breakpoint_type_t type, km_gva_t addr, size_t len)
{
   struct breakpoint_t* bp = bp_list_find(type, addr, len);

   km_infox(KM_TRACE_VCPU, "insert breakpoint type %d, addr 0x%lx, size %ld", type, addr, len);
   if ((bp = bp_list_find(type, addr, len)) != NULL) {
      bp->refcount++;
      return bp;
   }
   if ((bp = malloc(sizeof(struct breakpoint_t))) == NULL) {
      return NULL;
   }

   bp->addr = addr;
   bp->type = type;
   bp->len = len;
   bp->refcount = 1;

   switch (type) {
      case GDB_BREAKPOINT_SW:
         SLIST_INSERT_HEAD(&sw_breakpoints, bp, entries);
         break;

      case GDB_BREAKPOINT_HW:
      case GDB_WATCHPOINT_WRITE:
      case GDB_WATCHPOINT_READ:
      case GDB_WATCHPOINT_ACCESS:
         /* We only support hardware watchpoints. */
         if (nr_hw_breakpoints == MAX_HW_BREAKPOINTS) {
            return NULL;
         }
         nr_hw_breakpoints++;
         SLIST_INSERT_HEAD(&hw_breakpoints, bp, entries);
         break;

      default:
         assert("Unknown breakpoint type in insert" == NULL);
   }

   return bp;
}

/*
 * Removes a breakpoint from the list of breakpoints.
 * Returns -1 if the breakpoint is not in the list.
 */
static int bp_list_remove(gdb_breakpoint_type_t type, km_gva_t addr, size_t len)
{
   struct breakpoint_t* bp = NULL;

   if ((bp = bp_list_find(type, addr, len)) == NULL) {
      return -1;
   }
   if (--bp->refcount > 0) {
      return 0;
   }
   switch (type) {
      case GDB_BREAKPOINT_SW:
         SLIST_REMOVE(&sw_breakpoints, bp, breakpoint_t, entries);
         break;

      case GDB_BREAKPOINT_HW:
      case GDB_WATCHPOINT_WRITE:
      case GDB_WATCHPOINT_READ:
      case GDB_WATCHPOINT_ACCESS:
         /* We only support hardware watchpoints. */
         SLIST_REMOVE(&hw_breakpoints, bp, breakpoint_t, entries);
         nr_hw_breakpoints--;
         break;

      default:
         assert("Unknown breakpoint type in remove" == NULL);
   }
   free(bp);
   return 0;
}

static km_kma_t nextpage(km_kma_t addr)
{
   return (km_kma_t)rounddown(((uint64_t)addr + KM_PAGE_SIZE), KM_PAGE_SIZE);
}

/*
 * Read and hex encode guest memory correcting for SW breakpoints
 */
int km_guest_mem2hex(km_gva_t addr, km_kma_t kma, char* obuf, int len)
{
   unsigned char mbuf[len];
   int tmplen;
   int prot;
   int mbuf_index = 0;

   // Copy page by page checking permissions and enabling access if needed
   for (km_kma_t tmpkma = kma; tmpkma < kma + len;) {
      // Compute the amount of data to copy from this page.
      tmplen = nextpage(tmpkma) - tmpkma;
      if (tmplen > (kma + len) - tmpkma) {
         tmplen = (kma + len) - tmpkma;
      }

      // Unprotect if needed
      if (km_get_page_protection(tmpkma, &prot) != 0) {
         km_infox(KM_TRACE_GDB, "Couldn't get memory permissions for address %p", tmpkma);
         return -1;
      }
      km_kma_t aligned_kma = (km_kma_t)rounddown((uint64_t)tmpkma, KM_PAGE_SIZE);
      if ((prot & PROT_READ) == 0) {
         if (mprotect(aligned_kma, KM_PAGE_SIZE, prot | PROT_READ) != 0) {
            return -1;
         }
         km_infox(KM_TRACE_GDB, "Making page at %p temporarily readable", aligned_kma);
      }

      // Copy out the requested data.
      memcpy(&mbuf[mbuf_index], tmpkma, tmplen);
      tmpkma += tmplen;
      mbuf_index += tmplen;

      // Reprotect the page.
      if ((prot & PROT_READ) == 0 && mprotect(aligned_kma, KM_PAGE_SIZE, prot) != 0) {
         km_info(KM_TRACE_GDB, "Unable to remove read access to page at %p", aligned_kma);
         return -1;
      }
   }

   // Put instructions back where breakpoints have been planted
   for (int i = 0; i < len; i++) {
      struct breakpoint_t* bp;

      if ((bp = bp_list_find(GDB_BREAKPOINT_SW, addr + i, 1)) != NULL) {
         mbuf[i] = bp->saved_insn;
      }
   }
   mem2hex(mbuf, obuf, len);

   return 0;
}

/*
 * Convert count bytes of acsii hex in buf to binary and write to kma.
 * Make pages written to temporarily writable if necessary.
 * Return 0 on success.
 */
int km_guest_hex2mem(const char* buf, size_t count, km_kma_t kma)
{
   unsigned char mbuf[count/2];
   int len;
   int tmplen;
   int prot;
   int mbuf_index = 0;

   // Convert contents of buf to binary in mbuf
   len = hex2mem(buf, mbuf, count) - mbuf;

   // Copy data in mbuf[] into the guest's virtual address space pointed to by kma.
   for (km_kma_t tmpkma = kma; tmpkma < (kma + len); ) {
      // How much data can we write to this page.
      tmplen = nextpage(tmpkma) - tmpkma;
      if (tmplen > (kma + len) - tmpkma) {
         tmplen = (kma + len) - tmpkma;
      }

      // If needed unprotect this page to write the data.
      if (km_get_page_protection(tmpkma, &prot) != 0) {
         km_infox(KM_TRACE_GDB, "Couldn't get memory permissions for address %p", tmpkma);
         return -1;
      }
      km_kma_t aligned_kma = (km_kma_t)rounddown((uint64_t)tmpkma, KM_PAGE_SIZE);
      if ((prot & PROT_WRITE) == 0) {
         if (mprotect(aligned_kma, KM_PAGE_SIZE, prot | PROT_WRITE) != 0) {
            return -1;
         }
         km_infox(KM_TRACE_GDB, "Making page at %p temporarily writable", aligned_kma);
      }

      // Copy data into the guest's memory
      memcpy(tmpkma, &mbuf[mbuf_index], tmplen);
      tmpkma += tmplen;
      mbuf_index += tmplen;

      // Reprotect the page.
      if ((prot & PROT_WRITE) == 0 && mprotect(aligned_kma, KM_PAGE_SIZE, prot) != 0) {
         km_info(KM_TRACE_GDB, "Unable to remove write access to page at %p", aligned_kma);
         return -1;
      }
   }
   return 0;
}

/*
 * Fills *reg with a stream of hexadecimal digits for each guest register
 * in GDB register order, where each register is in target endian order.
 * Returns 0 if success, -1 otherwise.
 */
int km_gdb_read_registers(km_vcpu_t* vcpu, uint8_t* registers, size_t* len)
{
   struct kvm_fpu fpuregs;
   struct km_gdb_regs* gregs = (struct km_gdb_regs*)registers;
   int ret;

   if (*len < sizeof(struct km_gdb_regs)) {
      warnx("%s: buffer too small", __FUNCTION__);
      return -1;
   }
   km_read_registers(vcpu);
   km_read_sregisters(vcpu);

   *len = sizeof(struct km_gdb_regs);

   gregs->rax = vcpu->regs.rax;
   gregs->rbx = vcpu->regs.rbx;
   gregs->rcx = vcpu->regs.rcx;
   gregs->rdx = vcpu->regs.rdx;

   gregs->rsi = vcpu->regs.rsi;
   gregs->rdi = vcpu->regs.rdi;
   gregs->rbp = vcpu->regs.rbp;
   gregs->rsp = vcpu->regs.rsp;

   gregs->r8 = vcpu->regs.r8;
   gregs->r9 = vcpu->regs.r9;
   gregs->r10 = vcpu->regs.r10;
   gregs->r11 = vcpu->regs.r11;

   gregs->rip = vcpu->regs.rip;
   gregs->eflags = vcpu->regs.rflags;

   gregs->cs = vcpu->sregs.cs.selector;
   gregs->ss = vcpu->sregs.ss.selector;
   gregs->ds = vcpu->sregs.ds.selector;
   gregs->es = vcpu->sregs.es.selector;
   gregs->fs = vcpu->sregs.fs.selector;
   gregs->gs = vcpu->sregs.gs.selector;

   // TODO: Add KVM_GET_FPU
   if ((ret = ioctl(vcpu->kvm_vcpu_fd, KVM_GET_FPU, &fpuregs)) == -1) {
      warn("KVM_GET_FPU failed, ignoring");
   }
   // kvm gets 16 bytes per reg and names them differently. Will figure it out later
   km_infox(KM_TRACE_GDB, "FPU regs: not reporting for now (TODO)");
   return 0;
}

/*
 * Writes all guest registers from a stream of hexadecimal digits for each
 * register in *reg. Each register in *reg is in GDB register order, and in
 * target endian order.
 * Returns 0 if success, -1 otherwise.
 */
int km_gdb_write_registers(km_vcpu_t* vcpu, uint8_t* registers, size_t len)
{
   struct km_gdb_regs* gregs = (struct km_gdb_regs*)registers;

   if (len < sizeof(struct km_gdb_regs)) {
      return -1;
   }

   /* Let's read all registers just in case we miss filling one of them. */
   km_read_registers(vcpu);
   km_read_sregisters(vcpu);
   vcpu->regs.rax = gregs->rax;
   vcpu->regs.rbx = gregs->rbx;
   vcpu->regs.rcx = gregs->rcx;
   vcpu->regs.rdx = gregs->rdx;

   vcpu->regs.rsi = gregs->rsi;
   vcpu->regs.rdi = gregs->rdi;
   vcpu->regs.rbp = gregs->rbp;
   vcpu->regs.rsp = gregs->rsp;

   vcpu->regs.r8 = gregs->r8;
   vcpu->regs.r9 = gregs->r9;
   vcpu->regs.r10 = gregs->r10;
   vcpu->regs.r11 = gregs->r11;
   vcpu->regs.r12 = gregs->r12;
   vcpu->regs.r13 = gregs->r13;
   vcpu->regs.r14 = gregs->r14;
   vcpu->regs.r15 = gregs->r15;

   vcpu->regs.rip = gregs->rip;
   vcpu->regs.rflags = gregs->eflags;

   /*
    * Our CPU model doesn't have the concept of segments. We highjack the gdb
    * notion of x86_64 but our CPU model sets the segments once and they never
    * change after that.
    */
   assert(vcpu->sregs.cs.selector == gregs->cs && vcpu->sregs.ss.selector == gregs->ss &&
          vcpu->sregs.ds.selector == gregs->ds && vcpu->sregs.es.selector == gregs->es &&
          vcpu->sregs.fs.selector == gregs->fs && vcpu->sregs.gs.selector == gregs->gs);

   km_write_registers(vcpu);
   return 0;
}

/*
 * Add a breakpoint of type software or hardware, at address addr. len is
 * typically the size of the breakpoint in bytes that should be inserted
 * Returns 0 if success, -1 otherwise.
 */
int km_gdb_add_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len)
{
   struct breakpoint_t* bp;

   if (bp_list_find(type, addr, len) != NULL) {   // was already set
      return 0;
   }
   if ((bp = bp_list_insert(type, addr, len)) == NULL) {
      return -1;
   }
   if (type == GDB_BREAKPOINT_SW && kvm_arch_insert_sw_breakpoint(bp) != 0) {
      bp_list_remove(GDB_BREAKPOINT_SW, addr, len);
      return -1;
   }
   return km_gdb_update_guest_debug();
}

/*
 * Remove a breakpoint of type software or hardware, at address addr.
 * Returns 0 if success, -1 otherwise.
 */
int km_gdb_remove_breakpoint(gdb_breakpoint_type_t type, km_gva_t addr, size_t len)
{
   struct breakpoint_t* bp;

   if ((bp = bp_list_find(type, addr, len)) == NULL) {
      return 0;   // nothing to delete
   }
   if (type == GDB_BREAKPOINT_SW && kvm_arch_remove_sw_breakpoint(bp) != 0) {
      return -1;
   }
   if (bp_list_remove(type, addr, len) == -1) {
      return -1;
   }
   return km_gdb_update_guest_debug();
}

/*
 * Remove all active breakpoints, both hw and sw.
 * Return 0 for success, -1 if some breakpoints weren't removed.
 */
int km_gdb_remove_all_breakpoints(void)
{
   int rc;
   int finalrc = 0;
   breakpoint_t* bp;
   breakpoint_t* tempbp;

   SLIST_FOREACH_SAFE (bp, &sw_breakpoints, entries, tempbp) {
      rc = km_gdb_remove_breakpoint(bp->type, bp->addr, bp->len);
      if (rc != 0) {
         km_infox(KM_TRACE_GDB,
                  "Failed to remove sw breakpoint at gva 0x%lx, length %lu",
                  bp->addr,
                  bp->len);
         finalrc = rc;
      }
   }
   SLIST_FOREACH_SAFE (bp, &hw_breakpoints, entries, tempbp) {
      rc = km_gdb_remove_breakpoint(bp->type, bp->addr, bp->len);
      if (rc != 0) {
         km_infox(KM_TRACE_GDB,
                  "Failed to remove hw breakpoint at gva 0x%lx, length %lu",
                  bp->addr,
                  bp->len);
         finalrc = rc;
      }
   }

   return finalrc;
}

/*
 * Find a sw or hw breakpoint that covers the address in trigger_addr.
 * If a matching breakpoint was found return 0 and return bptype,
 * bpaddr, and bplen.
 * If no matching breakpoint was found return -1.
 */
int km_gdb_find_breakpoint(km_gva_t trigger_addr,
                           gdb_breakpoint_type_t* bptype,
                           km_gva_t* bpaddr,
                           size_t* bplen)
{
   struct breakpoint_t* bp;

   bp = bp_list_find_byaddr(trigger_addr);
   if (bp != NULL) {
      *bptype = bp->type;
      *bpaddr = bp->addr;
      *bplen = bp->len;
      return 0;
   }
   return -1;
}

/*
 * Enable single stepping. Returns 0 if success, -1 otherwise.
 */
int km_gdb_enable_ss(void)
{
   gdbstub.stepping = true;
   return km_gdb_update_guest_debug();
}

/*
 * Disable single stepping. Returns 0 if success, -1 otherwise.
 */
int km_gdb_disable_ss(void)
{
   gdbstub.stepping = false;
   return km_gdb_update_guest_debug();
}
