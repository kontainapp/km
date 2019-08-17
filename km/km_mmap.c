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
 *
 * Support for payload mmap() and related API
 */
#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_mem.h"

static inline void mmaps_lock(int needs_lock)
{
   if (needs_lock == 0) {
      return;
   }
   pthread_mutex_lock(&machine.mmaps.mutex);
}

static inline void mmaps_unlock(int needs_lock)
{
   if (needs_lock == 0) {
      return;
   }
   pthread_mutex_unlock(&machine.mmaps.mutex);
}

void km_guest_mmap_init(void)
{
   TAILQ_INIT(&machine.mmaps.free);
   TAILQ_INIT(&machine.mmaps.busy);
}

// on ubuntu and older kernels, this is not defined. We need symbol to check (and reject) flags
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Checks for stuff we do not support.
static inline int
mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   if (addr != 0) {   // TODO: We don't support address hints either, we simply ignore it for now.
      km_infox(KM_TRACE_MMAP, "Ignoring mmap hint 0x%lx", addr);
   }
   if (fd != -1 || offset != 0 || flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
      km_infox(KM_TRACE_MMAP, "mmap: wrong fd, offset or flags");
      return -EINVAL;
   }
   if (size % KM_PAGE_SIZE != 0) {
      km_infox(KM_TRACE_MMAP, "mmap: size misaligned");
      return -EINVAL;
   }
   if (size >= GUEST_MEM_ZONE_SIZE_VA) {
      km_infox(KM_TRACE_MMAP, "mmap: size is too large");
      return -ENOMEM;
   }
   return 0;
}

static inline int mumap_check_params(km_gva_t addr, size_t size)
{
   if (addr != roundup(addr, KM_PAGE_SIZE) || size == 0) {
      km_infox(KM_TRACE_MMAP, "munmap EINVAL 0x%lx size 0x%lx", addr, size);
      return -EINVAL;
   }
   return 0;
}

// find any free mmap larger or equal to 'size'
static km_mmap_reg_t* km_mmap_find_free(size_t size)
{
   km_mmap_reg_t* ptr;

   TAILQ_FOREACH (ptr, &machine.mmaps.free, link) {
      if (ptr->size >= size) {
         return ptr;
      }
   }
   return NULL;
}

// find an mmap in a list which includes the address. Returns NULL if not found
static km_mmap_reg_t* km_mmap_find_address(km_mmap_list_t* list, km_gva_t address)
{
   km_mmap_reg_t* ptr;

   TAILQ_FOREACH (ptr, list, link) {
      if (ptr->start + ptr->size <= address) {
         continue;   // still on the left of address
      }
      if (ptr->start > address) {
         return NULL;   // passed address already
      }
      return ptr;
   }
   return NULL;
}

// return 1 if ok to concat. Relies on 'free' mmaps all having 0 in protection and flags.
static inline int ok_to_concat(km_mmap_reg_t* left, km_mmap_reg_t* right)
{
   return (left->start + left->size == right->start && left->protection == right->protection &&
           left->flags == right->flags);
}

/*
 * Concatenate 'reg' mmap with left and/or right neighbor in the 'list', if they have the
 * same properties. Remove from the list and free the excess neighbors.
 */
static inline void km_mmap_concat(km_mmap_reg_t* reg, km_mmap_list_t* list)
{
   km_mmap_reg_t* left = TAILQ_PREV(reg, km_mmap_list, link);
   km_mmap_reg_t* right = TAILQ_NEXT(reg, link);

   assert(reg != left);
   assert(reg != right);
   km_infox(KM_TRACE_MMAP, "Concat check %p %p %p", left, reg, right);
   if (left != NULL && ok_to_concat(left, reg) == 1) {
      reg->start = left->start;
      reg->size += left->size;
      TAILQ_REMOVE(list, left, link);
      free(left);
   }
   if (right != NULL && ok_to_concat(reg, right) == 1) {
      reg->size += right->size;
      TAILQ_REMOVE(list, right, link);
      free(right);
   }
}

// wrapper for mprotect() on a single mmap region.
static void km_mmap_mprotect_region(km_mmap_reg_t* reg)
{
   if (mprotect(km_gva_to_kma_nocheck(reg->start), reg->size, reg->protection) != 0) {
      warn("%s: Failed to mprotect addr 0x%lx sz 0x%lx prot 0x%x)",
           __FUNCTION__,
           reg->start,
           reg->size,
           reg->protection);
   }
}

/*
 * Inserts region into the list sorted by reg->start.
 */
static inline void km_mmap_insert(km_mmap_reg_t* reg, km_mmap_list_t* list)
{
   km_mmap_reg_t* ptr;

   if (TAILQ_EMPTY(list)) {
      TAILQ_INSERT_HEAD(list, reg, link);
   } else {
      TAILQ_FOREACH (ptr, list, link) {   // find the map to the right of the 'reg'
         if (ptr->start > reg->start) {
            assert(ptr->start >= reg->start + reg->size);
            break;
         }
         // double check that there are no overlaps (we don't support overlapping mmaps)
         assert(ptr->start < reg->start && (ptr->start + ptr->size <= reg->start));
      }

      if (ptr == TAILQ_END(list)) {
         TAILQ_INSERT_TAIL(list, reg, link);
      } else {
         TAILQ_INSERT_BEFORE(ptr, reg, link);
      }
   }
   km_mmap_mprotect_region(reg);
}

// Insert a region into busy mmaps with proper protection. Expects 'reg' to be malloced
static inline void km_mmap_insert_busy(km_mmap_reg_t* reg)
{
   km_mmap_insert(reg, &machine.mmaps.busy);
}

// Inserts 'reg' after 'listelem' in busy. No list traverse.
static inline void km_mmap_insert_busy_after(km_mmap_reg_t* listelem, km_mmap_reg_t* reg)
{
   TAILQ_INSERT_AFTER(&machine.mmaps.busy, listelem, reg, link);
}

// Inserts 'reg' before 'listelem' in busy. No list traverse.
static inline void km_mmap_insert_busy_before(km_mmap_reg_t* listelem, km_mmap_reg_t* reg)
{
   // we don't care which list, but keep function name for consistency
   TAILQ_INSERT_BEFORE(listelem, reg, link);
}

// Insert 'reg' into the FREE MMAPS with PROT_NONE, and compress maps/tbrk. Expects 'reg' to be malloced
static inline void km_mmap_insert_free(km_mmap_reg_t* reg)
{
   km_mmap_list_t* list = &machine.mmaps.free;

   reg->protection = PROT_NONE;
   reg->flags = 0;
   km_mmap_insert(reg, list);
   km_mmap_concat(reg, list);
   if (reg->start == km_mem_tbrk(0)) {   // adjust tbrk() if needed
      km_mem_tbrk(reg->start + reg->size);
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   }
}

static inline void km_mmap_remove_busy(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&machine.mmaps.busy, reg, link);
}

// Remove an element from a list. In the list, this does not depend on list head
static inline void km_mmap_remove_free(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&machine.mmaps.free, reg, link);
}

// moves existing mmap region from busy to free
static inline void km_mmap_move_to_free(km_mmap_reg_t* reg)
{
   km_mmap_remove_busy(reg);
   km_mmap_insert_free(reg);
}

/*
 * Maps an address range in guest virtual space.
 *
 * TODO: check flags. Use the gva if specified.
 *
 * Returns mapped addres on success, or -errno on failure.
 * -EINVAL if the args are not valid
 * -ENOMEM if fails to allocate memory
 */
static km_gva_t
km_guest_mmap_lockable(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset, int needs_lock)
{
   km_gva_t ret;
   km_mmap_reg_t* reg;

   km_infox(KM_TRACE_MMAP, "mmap guest(0x%lx, 0x%lx, prot 0x%x flags 0x%x)", gva, size, prot, flags);
   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   mmaps_lock(needs_lock);
   if ((reg = km_mmap_find_free(size)) != NULL) {   // found a 'free' mmap to accommodate the request
      assert(size <= reg->size);
      if (reg->size > size) {   // free mmap has extra room to be kept in 'free'
         km_mmap_reg_t* busy;
         if ((busy = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            mmaps_unlock(needs_lock);
            return -ENOMEM;
         }
         *busy = *reg;
         reg->start += size;   // patch the region in 'free' list to keep only extra room
         reg->size -= size;
         busy->size = size;
         reg = busy;   // it will be inserted into 'busy' list
      } else {         // the 'free' mmap has exactly the requested size
         assert(reg->size == size);
         km_mmap_remove_free(reg);
      }
   } else {   // nothing useful in the free list, get fresh memory by moving tbrk down
      if ((reg = malloc(sizeof(km_mmap_reg_t))) == NULL) {
         mmaps_unlock(needs_lock);
         return -ENOMEM;
      }
      km_gva_t want = machine.tbrk - size;
      if ((ret = km_mem_tbrk(want)) != want) {
         mmaps_unlock(needs_lock);
         free(reg);
         return ret;
      }
      reg->start = ret;   //  place requested mmap region in the newly allocated memory
      reg->size = size;
   }
   reg->flags = flags;
   reg->protection = prot;
   km_gva_t map_start = reg->start;   // reg->start can be modified if there is concat in insert_busy
   km_mmap_insert_busy(reg);
   mmaps_unlock(needs_lock);
   return map_start;
}

// mmap under a pre-acquired lock
static km_gva_t
km_guest_mmap_nolock(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   return km_guest_mmap_lockable(gva, size, prot, flags, fd, offset, 0);
}

// mmap with locking
km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   return km_guest_mmap_lockable(gva, size, prot, flags, fd, offset, 1);
}

typedef void (*km_mmap_action)(km_mmap_reg_t*);
/*
 * Apply 'action(reg)' to all mmap regions completely within the (addr, addr+size-1) range in
 * 'busy' machine.mmaps. If needed splits the mmaps at the ends - new maps are also updated to
 * have protection 'prot',
 *
 * Returns 0 on success or -errno
 */
static int km_mmap_busy_range_apply(km_gva_t addr, size_t size, km_mmap_action action, int prot)
{
   km_mmap_reg_t* reg;

   TAILQ_FOREACH (reg, &machine.mmaps.busy, link) {
      km_mmap_reg_t* extra;

      if (reg->start + reg->size <= addr) {
         continue;   // skip all to the left of addr
      }
      if (reg->start >= addr + size) {
         break;   // we passed the range and are done
      }
      if (reg->start < addr) {   // overlaps on the start
         if ((extra = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            return -ENOMEM;
         }
         *extra = *reg;
         reg->size = addr - reg->start;   // left part, to keep in busy
         extra->start = addr;             // right part, to insert in busy
         extra->size -= reg->size;
         assert(reg != extra);
         km_mmap_insert_busy_after(reg, extra);   // next action need to process 'extra', not 'reg'
         continue;
      }
      if (reg->start + reg->size > addr + size) {   // overlaps on the end
         if ((extra = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            return -ENOMEM;
         }
         *extra = *reg;
         reg->size = addr + size - reg->start;   // left part , to insert in busy
         extra->start = addr + size;             // right part, to keep in busy
         extra->size -= reg->size;
         assert(reg != extra);
         km_mmap_insert_busy_after(reg, extra);   // fall through to process 'reg'
      }
      assert(reg->start >= addr && reg->start + reg->size <= addr + size);   // fully within the range
      reg->protection = prot;
      action(reg);
   }
   return 0;
}

/*
 * Unmaps address range in guest virtual space.
 * Per per munmap(3) it is ok if some pages within the range were already unmapped.
 *
 * Returns 0 on success.
 * -EINVAL if the part of the FULL requested region is not mapped
 * -ENOMEM if fails to allocate memory for control structures
 */
static int km_guest_munmap_lockable(km_gva_t addr, size_t size, int needs_lock)
{
   int ret;

   km_infox(KM_TRACE_MMAP, "munmap guest(0x%lx, 0x%lx)", addr, size);
   size = roundup(size, KM_PAGE_SIZE);
   if ((ret = mumap_check_params(addr, size)) != 0) {
      return ret;
   }
   mmaps_lock(needs_lock);
   ret = km_mmap_busy_range_apply(addr, size, km_mmap_move_to_free, PROT_NONE);
   mmaps_unlock(needs_lock);
   km_info(KM_TRACE_MMAP, "== munmap ret=%d", ret);
   return ret;
}

// unmap that always acquires mmaps lock.
int km_guest_munmap(km_gva_t addr, size_t size)
{
   return km_guest_munmap_lockable(addr, size, 1);
}

// unmap under pre-acquired lock.
static int km_guest_munmap_nolock(km_gva_t addr, size_t size)
{
   return km_guest_munmap_lockable(addr, size, 0);
}

/*
 * Checks if busy mmaps are contiguous from `addr' to `addr+size'.
 * Return 0 if they are, -1 if they are not.
 */
static int km_mmap_busy_check_contigious(km_gva_t addr, size_t size)
{
   km_mmap_reg_t* reg;
   km_gva_t last_end = 0;

   TAILQ_FOREACH (reg, &machine.mmaps.busy, link) {
      if (reg->start + reg->size < addr) {
         continue;
      }
      if (reg->start > addr + size) {
         if (last_end >= addr + size) {
            return 0;
         }
         return -1;   // gap at the end of the range
      }
      if ((last_end != 0 && last_end != reg->start) || (last_end == 0 && reg->start > addr)) {
         return -1;   // gap in the beginning or before current reg
      }
      last_end = reg->start + reg->size;
   }
   return 0;
}

/*
 * Changes protection for contigious range of mmap-ed memory.
 * With locked == 1 does not bother to do locking
 * returns 0 (success) or -errno per mprotect(3)
 */
int km_guest_mprotect(km_gva_t addr, size_t size, int prot)
{
   int needs_lock = 1;   // always lock
   km_infox(KM_TRACE_MMAP, "mprotect guest(0x%lx 0x%lx prot %x)", addr, size, prot);
   if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_GROWSDOWN | PROT_GROWSUP)) != 0) {
      return -EINVAL;
   }
   if (addr != rounddown(addr, KM_PAGE_SIZE) || (size = roundup(size, KM_PAGE_SIZE)) == 0) {
      return -EINVAL;
   }
   mmaps_lock(needs_lock);
   // Per mprotect(3) if there are un-mmaped pages in the area, error out with ENOMEM
   if (km_mmap_busy_check_contigious(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "mprotect area not fully mapped");
      mmaps_unlock(needs_lock);
      return -ENOMEM;
   }
   if (km_mmap_busy_range_apply(addr, size, km_mmap_mprotect_region, prot) != 0) {
      mmaps_unlock(needs_lock);
      return -ENOMEM;
   }
   mmaps_unlock(needs_lock);
   return 0;
}

// Grows a mmap to size. old_addr is expected to be within ptr map. Returns new address or -errno
static km_gva_t
km_mremap_grow(km_mmap_reg_t* ptr, km_gva_t old_addr, size_t old_size, size_t size, int may_move)
{
   assert(old_addr >= ptr->start && old_addr < ptr->start + ptr->size &&
          old_addr + old_size <= ptr->start + ptr->size && size > old_size);

   if (ptr->start + ptr->size >= old_addr + size) {
      km_infox(KM_TRACE_MMAP, "mremap: enough room in mmaps, noop");
      return old_addr;
   }
   // If there is a large enough free slot after this, use it.
   km_mmap_reg_t* next = TAILQ_NEXT(ptr, link);
   size_t needed = size - old_size;
   if (next != NULL && next->start - (ptr->start + ptr->size) >= needed) {
      // Adjust free list and protections -  cannot use km_guest_mmap here as it can grab a wrong area
      km_infox(KM_TRACE_MMAP, "mremap: reusing adjusted free map");
      km_mmap_reg_t* donor = km_mmap_find_address(&machine.mmaps.free, ptr->start + ptr->size);
      assert(donor != NULL && donor->size >= needed);   // MUST have free slot due to gap in busy
      ptr->size += needed;
      km_mmap_mprotect_region(ptr);
      if (donor->size == needed) {
         km_mmap_remove_free(donor);
         free(donor);
      } else {
         donor->start += needed;
         donor->size -= needed;
      }
      return old_addr;
   }

   // No free space to grow, alloc new
   km_gva_t ret;
   if (may_move == 0 ||
       (ret = km_syscall_ok(km_guest_mmap_nolock(0, size, ptr->protection, ptr->flags, -1, 0))) == -1) {
      km_info(KM_TRACE_MMAP, "Failed to get mmap for growth (may_move = %d)", may_move);
      return -ENOMEM;
   }
   void* to = km_gva_to_kma(ret);
   void* from = km_gva_to_kma(old_addr);
   assert(from != NULL);   // should have been checked before, in hcalls
   memcpy(to, from, old_size);
   if (km_syscall_ok(km_guest_munmap_nolock(old_addr, old_size)) == -1) {
      err(1, "Failed to unmap after remapping");
   }
   return ret;
}

// Shrinks a mmap to size. old_addr is expected to be within ptr map. Returns new address or -errno
static km_gva_t km_mremap_shrink(km_mmap_reg_t* ptr, km_gva_t old_addr, size_t old_size, size_t size)
{
   assert(old_addr >= ptr->start && old_addr < ptr->start + ptr->size &&
          old_addr + old_size <= ptr->start + ptr->size);
   if (km_syscall_ok(km_guest_munmap_nolock(old_addr + size, old_size - size)) == -1) {
      return -EFAULT;
   }
   return old_addr;
}

// Implementation of guest 'mremap'. Returns new address or -errno
km_gva_t
km_guest_mremap(km_gva_t old_addr, size_t old_size, size_t size, int flags, ... /*void* new_address*/)
{
   km_mmap_reg_t* ptr;
   km_gva_t ret;
   int needs_lock = 1;   // always lock

   km_infox(KM_TRACE_MMAP, "mremap(0x%lx, 0x%lx, 0x%lx, 0x%x)", old_addr, old_size, size, flags);
   if ((old_addr % KM_PAGE_SIZE) != 0 || old_size == 0 || size == 0 || (flags & ~MREMAP_MAYMOVE) != 0) {
      return -EINVAL;
   }

   old_size = roundup(old_size, KM_PAGE_SIZE);
   size = roundup(size, KM_PAGE_SIZE);
   if (old_size == size) {   // no changes requested
      return old_addr;
   }

   mmaps_lock(needs_lock);
   if ((ptr = km_mmap_find_address(&machine.mmaps.busy, old_addr)) == NULL) {
      mmaps_unlock(needs_lock);
      km_infox(KM_TRACE_MMAP, "mremap: Did not find requested map");
      return -EFAULT;
   }
   if (ptr->start + ptr->size < old_addr + old_size) {   // check the requested map is homogeneous
      mmaps_unlock(needs_lock);
      km_infox(KM_TRACE_MMAP, "mremap: requested mmap is not fully within existing map");
      return -EFAULT;
   }
   // found the map and it's a legit size
   if (old_size < size) {   // grow
      ret = km_mremap_grow(ptr, old_addr, old_size, size, (flags & MREMAP_MAYMOVE) != 0);
   } else {   // shrink (old_size == size is already checked above)
      ret = km_mremap_shrink(ptr, old_addr, old_size, size);
   }
   mmaps_unlock(needs_lock);
   km_infox(KM_TRACE_MMAP, "mremap: ret=0x%lx", ret);
   return ret;
}

/*
 * Need access to mmaps to drop core, so this is here for now.
 */
void km_dump_core(km_vcpu_t* vcpu, x86_interrupt_frame_t* iframe)
{
   char* core_path = km_get_coredump_path();
   int fd;
   int phnum = 1;   // 1 for PT_NOTE
   size_t offset;   // Data offset
   km_mmap_reg_t* ptr;
   char* notes_buffer;
   size_t notes_length = km_core_notes_length();
   size_t notes_used = 0;

   if ((fd = open(core_path, O_RDWR | O_CREAT | O_TRUNC, 0666)) < 0) {
      errx(2, "Cannot open corefile '%s' - %s\n", core_path, strerror(errno));
   }
   warnx("Write coredump to '%s'", core_path);

   if ((notes_buffer = (char*)malloc(notes_length)) == NULL) {
      errx(2, "%s - cannot allocate notes buffer", __FUNCTION__);
   }
   memset(notes_buffer, 0, notes_length);

   // Count up phdrs for mmaps and set offset where data will start.
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      phnum++;
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      phnum++;
   }
   offset = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);

   // write elf header
   km_core_write_elf_header(fd, phnum);
   // Create PT_NOTE in memory and write the header
   notes_used = km_core_write_notes(vcpu, fd, offset, notes_buffer, notes_length);
   offset += notes_used;
   // Write headers for segments from ELF
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      km_core_write_load_header(fd,
                                offset,
                                km_guest.km_phdr[i].p_vaddr,
                                km_guest.km_phdr[i].p_memsz,
                                km_guest.km_phdr[i].p_flags);
      offset += km_guest.km_phdr[i].p_memsz;
   }
   // Headers for MMAPs
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      // translate mmap prot to elf access flags.
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      static uint8_t mmap_to_elf_flags[8] =
          {0, PF_R, PF_W, (PF_R | PF_W), PF_X, (PF_R | PF_X), (PF_W | PF_X), (PF_R | PF_W | PF_X)};

      km_core_write_load_header(fd, offset, ptr->start, ptr->size, mmap_to_elf_flags[ptr->protection & 0x7]);
      offset += ptr->size;
   }

   // Write the actual data.
   km_core_write(fd, notes_buffer, notes_length);
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      km_guestmem_write(fd, km_guest.km_phdr[i].p_vaddr, km_guest.km_phdr[i].p_memsz);
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      km_kma_t start = km_gva_to_kma_nocheck(ptr->start);
      // make sure we can read the mapped memory (e.g. it can be EXEC only)
      if ((ptr->protection & PROT_READ) != PROT_READ) {
         if (mprotect(start, ptr->size, ptr->protection | PROT_READ) != 0) {
            err(1, "%s: failed to make %p,0x%lx readable for dump", __FUNCTION__, start, ptr->size);
         }
      }
      km_guestmem_write(fd, ptr->start, ptr->size);
      // recover protection, in case it's a live coredump and we are not exiting yet
      if (mprotect(start, ptr->size, ptr->protection) != 0) {
         err(1, "%s: failed to set %p,0x%lx prot to 0x%x", __FUNCTION__, start, ptr->size, ptr->protection);
      }
   }

   free(notes_buffer);
   (void)close(fd);
}