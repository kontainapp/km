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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_filesys.h"
#include "km_mem.h"

static const bool MMAP_ALLOC_MONITOR = true;
static const bool MMAP_ALLOC_GUEST = false;

static inline void mmaps_lock(void)
{
   pthread_mutex_lock(&machine.mmaps.mutex);
}

static inline void mmaps_unlock(void)
{
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

void km_mmap_show_map(const km_mmap_list_t* list, const char* name)
{
   km_mmap_reg_t* reg;

   km_infox(KM_TRACE_MMAP, "%s", name);
   TAILQ_FOREACH (reg, list, link) {
      km_infox(KM_TRACE_MMAP,
               "start 0x%lx size 0x%lx flags 0x%x protection 0x%x km_flags 0x%x gfd 0x%x offset "
               "0x%lx",
               reg->start,
               reg->size,
               reg->flags,
               reg->protection,
               reg->km_flags,
               reg->gfd,
               reg->offset);
   }
}

void km_mmap_show_maps()
{
   km_mmap_show_map(&machine.mmaps.free, "Free list");
   km_mmap_show_map(&machine.mmaps.busy, "Busy list");
}

// Checks for stuff we do not support.
static inline int
mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   if (flags & MAP_FIXED_NOREPLACE) {
      km_infox(KM_TRACE_MMAP, "mmap: wrong fd, offset or flags");
      return -EINVAL;
   }
   if ((flags & MAP_FIXED) && addr == 0) {
      km_infox(KM_TRACE_MMAP, "mmap: bad fixed address");
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
           left->flags == right->flags && left->km_flags == right->km_flags);
}

/*
 * Concatenate 'reg' mmap with left and/or right neighbor in the 'list', if they have the
 * same properties. Remove from the list and free the excess neighbors.
 */
static inline void km_mmap_concat(km_mmap_reg_t* reg, km_mmap_list_t* list)
{
   km_mmap_reg_t* left = TAILQ_PREV(reg, km_mmap_list, link);
   km_mmap_reg_t* right = TAILQ_NEXT(reg, link);

   assert(reg != left && reg != right);   // out of paranoia, check for cycles
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

/*
 * If this is the first time region will be accessible, zero it out
 *
 * This function relies on the fact that the last step in any guest mmap manipulation is always
 * setting protection to whatever the guest has requested. So right after a call to km_mmap_zero
 * there is always proper protection setting - so we can simply set it to PROT_WRITE here before
 * memset
 */
static void km_mmap_zero(km_mmap_reg_t* reg)
{
   km_kma_t addr = km_gva_to_kma_nocheck(reg->start);
   size_t size = reg->size;

   if (reg->protection == PROT_NONE || (reg->km_flags & KM_MMAP_INITED) != 0) {
      km_infox(KM_TRACE_MMAP,
               "skip zero kma: %p sz %ld prot 0x%x km_flags 0x%x",
               addr,
               size,
               reg->protection,
               reg->km_flags);
      return;   // not accessible or already was zeroed out
   }
   km_infox(KM_TRACE_MMAP, "zero km %p sz %ld", addr, size);
   mprotect(addr, size, PROT_WRITE);
   memset(addr, 0, size);
   reg->km_flags |= KM_MMAP_INITED;
}

// wrapper for mprotect() on a single mmap region.
static void km_mmap_mprotect_region(km_mmap_reg_t* reg)
{
   km_mmap_zero(reg);   // range may become acessible for the 1st time - zero it if needed

   if (mprotect(km_gva_to_kma_nocheck(reg->start), reg->size, reg->protection) != 0) {
      warn("%s: Failed to mprotect addr 0x%lx sz 0x%lx prot 0x%x)",
           __FUNCTION__,
           reg->start,
           reg->size,
           reg->protection);
   }
}

// mprotects 'reg' and concats with adjustment neighbors
static inline void km_mmap_mprotect(km_mmap_reg_t* reg)
{
   km_mmap_mprotect_region(reg);
   km_mmap_concat(reg, &machine.mmaps.busy);
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
   km_mmap_list_t* list = &machine.mmaps.busy;
   km_mmap_insert(reg, list);
   km_mmap_concat(reg, list);
}

// Inserts 'reg' after 'listelem' in busy. No list traverse and no neighbor concatenation
static inline void km_mmap_insert_busy_after(km_mmap_reg_t* listelem, km_mmap_reg_t* reg)
{
   TAILQ_INSERT_AFTER(&machine.mmaps.busy, listelem, reg, link);
}

// Inserts 'reg' before 'listelem' in busy. No list traverse and no neighbor concatenation
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
   reg->gfd = -1;
   km_mmap_insert(reg, list);
   reg->km_flags &= ~KM_MMAP_INITED;   // allow concat to happen on free list
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

      if ((reg->km_flags & KM_MMAP_MONITOR) == KM_MMAP_MONITOR) {
         warnx("munmap/mprotect called on monitor allocated range requested addr 0x%lx size 0x%lx "
               "region start 0x%lx size 0x%lx",
               addr,
               size,
               reg->start,
               reg->size);
         continue;
      }
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
         km_mmap_insert_busy_after(reg, extra);
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
         km_mmap_insert_busy_after(reg, extra);   // fall through to process 'reg'
      }
      assert(reg->start >= addr && reg->start + reg->size <= addr + size);   // fully within the range
      reg->protection = prot;
      action(reg);
   }
   return 0;
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

// Guest mprotect implementation. Params should be already checked and locks taken.
static int km_guest_mprotect_nolock(km_gva_t addr, size_t size, int prot)
{
   // Per mprotect(3) if there are un-mmaped pages in the area, error out with ENOMEM
   if (km_mmap_busy_check_contigious(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "mprotect area not fully mapped");
      return -ENOMEM;
   }
   if (km_mmap_busy_range_apply(addr, size, km_mmap_mprotect, prot) != 0) {
      return -ENOMEM;
   }
   return 0;
}

static km_mmap_reg_t* km_find_reg_nolock(km_gva_t gva)
{
   km_mmap_reg_t* reg;
   TAILQ_FOREACH (reg, &machine.mmaps.busy, link) {
      if (reg->start <= gva && reg->start + reg->size > gva) {
         return reg;
      }
   }
   return NULL;
}

// Guest mmap implementation. Params should be already checked and locks taken..
static km_gva_t
km_guest_mmap_nolock(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset, bool allocation_type)
{
   km_mmap_reg_t* reg;
   km_gva_t ret;

   if ((flags & MAP_FIXED)) {
      // Only allow MAP_FIXED for already mapped regions (for MUSL dynlink.c)
      if ((reg = km_find_reg_nolock(gva)) == NULL || reg->start + reg->size < gva + size) {
         return -EINVAL;
      }
      return gva;
   }
   if ((reg = km_mmap_find_free(size)) != NULL) {   // found a 'free' mmap to accommodate the request
      assert(size <= reg->size);
      if (reg->size > size) {   // free mmap has extra room to be kept in 'free'
         km_mmap_reg_t* busy;
         if ((busy = malloc(sizeof(km_mmap_reg_t))) == NULL) {
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
         return -ENOMEM;
      }
      km_gva_t want = machine.tbrk - size;
      if ((ret = km_mem_tbrk(want)) != want) {
         free(reg);
         return ret;
      }
      reg->start = ret;   //  place requested mmap region in the newly allocated memory
      reg->size = size;
   }
   reg->flags = flags;
   reg->protection = prot;
   reg->km_flags = ((allocation_type == MMAP_ALLOC_MONITOR) ? KM_MMAP_MONITOR : 0);
   reg->gfd = fd;
   reg->offset = offset;
   km_gva_t map_start = reg->start;   // reg->start can be modified if there is concat in insert_busy
   km_mmap_insert_busy(reg);
   return map_start;
}

/*
 * Maps an address range in guest virtual space.
 *  - checks the params, takes the lock and calls the actual implementation
 *
 * Returns mapped addres on success, or -errno on failure.
 * -EINVAL if the args are not valid
 * -ENOMEM if fails to allocate memory
 */
static km_gva_t
km_guest_mmap_impl(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset, bool allocation_type)
{
   km_gva_t ret;
   km_infox(KM_TRACE_MMAP,
            "mmap guest(0x%lx, 0x%lx, prot 0x%x flags 0x%x allocation type 0x%x)",
            gva,
            size,
            prot,
            flags,
            allocation_type);
   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   mmaps_lock();
   ret = km_guest_mmap_nolock(gva, size, prot, flags, fd, offset, allocation_type);
   mmaps_unlock();
   if (km_syscall_ok(ret) && fd >= 0) {
      km_kma_t kma = km_gva_to_kma(ret);
      int hfd = guestfd_to_hostfd(fd);
      if (hfd >= 0) {
         if (mmap(kma, size, prot, flags | MAP_FIXED, hfd, offset) == (void*)-1) {
            ret = -errno;
            // TODO (muth): undo mmap operation from above.
            warn("%s: file mmap failed", __FUNCTION__);
         }
      } else {
         ret = -EINVAL;
      }
   }
   km_infox(KM_TRACE_MMAP, "== mmap guest ret=0x%lx", ret);
   return ret;
}

/*
 * wrapper to km_guest_mmap_impl for calls from guest
 */
km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   return (km_guest_mmap_impl(gva, size, prot, flags, fd, offset, MMAP_ALLOC_GUEST));
}

/*
 * wrapper to km_guest_mmap_impl for calls from monitor
 * also helps avoid polluting all callers with mmap.h, and set
 * return value and errno
 */
km_gva_t km_guest_mmap_simple_monitor(size_t size)
{
   return km_syscall_ok(km_guest_mmap_impl(
       0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, MMAP_ALLOC_MONITOR));
}

/*
 * wrapper to km_guest_mmap_impl for calls from monitor as a side affect of guest
 * also helps avoid polluting all callers with mmap.h, and set
 * return value and errno
 */
km_gva_t km_guest_mmap_simple(size_t size)
{
   return km_syscall_ok(
       km_guest_mmap_impl(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, MMAP_ALLOC_GUEST));
}

// Guest munmap implementation. Params should be already checked and locks taken.
static int km_guest_munmap_nolock(km_gva_t addr, size_t size)
{
   return km_mmap_busy_range_apply(addr, size, km_mmap_move_to_free, PROT_NONE);
}

/*
 * Unmaps address range in guest virtual space.
 * Per per munmap(3) it is ok if some pages within the range were already unmapped.
 *
 * Returns 0 on success.
 * -EINVAL if the part of the FULL requested region is not mapped
 * -ENOMEM if fails to allocate memory for control structures
 */
int km_guest_munmap(km_gva_t addr, size_t size)
{
   int ret;

   km_infox(KM_TRACE_MMAP, "munmap guest(0x%lx, 0x%lx)", addr, size);
   size = roundup(size, KM_PAGE_SIZE);
   if ((ret = mumap_check_params(addr, size)) != 0) {
      return ret;
   }
   mmaps_lock();
   ret = km_guest_munmap_nolock(addr, size);
   mmaps_unlock();
   km_infox(KM_TRACE_MMAP, "== munmap ret=%d", ret);
   return ret;
}

/*
 * Changes protection for contigious range of mmap-ed memory.
 * With locked == 1 does not bother to do locking
 * returns 0 (success) or -errno per mprotect(3)
 */
int km_guest_mprotect(km_gva_t addr, size_t size, int prot)
{
   km_infox(KM_TRACE_MMAP, "mprotect guest(0x%lx 0x%lx prot %x)", addr, size, prot);
   if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_GROWSDOWN | PROT_GROWSUP)) != 0) {
      return -EINVAL;
   }
   if (addr != rounddown(addr, KM_PAGE_SIZE) || (size = roundup(size, KM_PAGE_SIZE)) == 0) {
      return -EINVAL;
   }
   mmaps_lock();
   int ret = km_guest_mprotect_nolock(addr, size, prot);
   mmaps_unlock();
   return ret;
}

// Grows a mmap to size. old_addr is expected to be within ptr map. Returns new address or -errno
static km_gva_t
km_mremap_grow(km_mmap_reg_t* ptr, km_gva_t old_addr, size_t old_size, size_t size, int may_move)
{
   km_mmap_reg_t* next;
   size_t needed = size - old_size;

   assert((ptr->km_flags & KM_MMAP_MONITOR) == 0);
   assert(old_addr >= ptr->start && old_addr < ptr->start + ptr->size &&
          old_addr + old_size <= ptr->start + ptr->size && size > old_size);

   // If there is a large enough free slot after this, use it.
   if ((ptr->start + ptr->size == old_addr + old_size) && (next = TAILQ_NEXT(ptr, link)) != NULL &&
       next->start - (ptr->start + ptr->size) >= needed) {
      // Adjust free list and protections - cannot use km_guest_mmap here as it can grab a wrong area
      km_infox(KM_TRACE_MMAP, "mremap: reusing adjusted free map");
      km_mmap_reg_t* donor = km_mmap_find_address(&machine.mmaps.free, ptr->start + ptr->size);
      assert(donor != NULL && donor->size >= needed);   // MUST have free slot due to gap in busy
      km_mmap_reg_t new_range = {.start = old_addr + old_size,
                                 .size = needed,
                                 .protection = ptr->protection,
                                 .flags = ptr->flags,
                                 .km_flags = 0};
      km_mmap_zero(&new_range);
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
       (ret = km_syscall_ok(
            km_guest_mmap_nolock(0, size, ptr->protection, ptr->flags, -1, 0, MMAP_ALLOC_GUEST))) ==
           -1) {
      km_info(KM_TRACE_MMAP, "Failed to get mmap for growth (may_move = %d)", may_move);
      return -ENOMEM;
   }
   void* to = km_gva_to_kma(ret);
   void* from = km_gva_to_kma(old_addr);
   assert(from != NULL);         // should have been checked before, in hcalls
   memcpy(to, from, old_size);   // WARNING: this may be slow, see issue #198
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

// Guest mremap implementation. Params should be already checked and locks taken.
static km_gva_t km_guest_mremap_nolock(km_gva_t old_addr, size_t old_size, size_t size, int flags)
{
   km_mmap_reg_t* ptr;
   km_gva_t ret;

   if ((ptr = km_mmap_find_address(&machine.mmaps.busy, old_addr)) == NULL) {
      km_infox(KM_TRACE_MMAP, "mremap: Did not find requested map");
      return -EFAULT;
   }
   if (ptr->start + ptr->size < old_addr + old_size) {   // check the requested map is homogeneous
      km_infox(KM_TRACE_MMAP, "mremap: requested mmap is not fully within existing map");
      return -EFAULT;
   }
   if ((ptr->km_flags & KM_MMAP_MONITOR) == KM_MMAP_MONITOR) {
      return -EFAULT;
   }
   // found the map and it's a legit size
   if (old_size < size) {   // grow
      ret = km_mremap_grow(ptr, old_addr, old_size, size, (flags & MREMAP_MAYMOVE) != 0);
   } else {   // shrink (old_size == size is already checked above)
      ret = km_mremap_shrink(ptr, old_addr, old_size, size);
   }
   return ret;
}

// Implementation of guest 'mremap'. Returns new address or -errno
km_gva_t
km_guest_mremap(km_gva_t old_addr, size_t old_size, size_t size, int flags, ... /*void* new_address*/)
{
   km_gva_t ret;

   km_infox(KM_TRACE_MMAP, "mremap(0x%lx, 0x%lx, 0x%lx, 0x%x)", old_addr, old_size, size, flags);
   if ((old_addr % KM_PAGE_SIZE) != 0 || old_size == 0 || size == 0 || (flags & ~MREMAP_MAYMOVE) != 0) {
      return -EINVAL;
   }

   old_size = roundup(old_size, KM_PAGE_SIZE);
   size = roundup(size, KM_PAGE_SIZE);
   if (old_size == size) {   // no changes requested
      return old_addr;
   }

   mmaps_lock();
   ret = km_guest_mremap_nolock(old_addr, old_size, size, flags);
   mmaps_unlock();
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
   km_gva_t end_load = 0;

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
      km_gva_t pend = km_guest.km_phdr[i].p_vaddr + km_guest.km_phdr[i].p_memsz;
      if (pend > end_load) {
         end_load = pend;
      }

      phnum++;
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      phnum++;
   }
   // Account for brk beyond elf segments.
   if (end_load != 0 && end_load < machine.brk) {
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
   // HDR for space between end of elf load and brk
   if (end_load != 0 && end_load < machine.brk) {
      km_core_write_load_header(fd, offset, end_load, machine.brk - end_load, PF_R | PF_W);
      offset += machine.brk - end_load;
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
   // Data for space between end of elf load and brk
   if (end_load != 0 && end_load < machine.brk) {
      km_guestmem_write(fd, end_load, machine.brk - end_load);
   }

   free(notes_buffer);
   (void)close(fd);
}