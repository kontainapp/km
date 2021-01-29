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
#include "km_filesys.h"
#include "km_mem.h"

typedef enum { MMAP_ALLOC_GUEST = 0x0, MMAP_ALLOC_MONITOR } mmap_allocation_type_e;

static inline void mmaps_lock(void)
{
   km_mutex_lock(&machine.mmaps.mutex);
}

static inline void mmaps_unlock(void)
{
   km_mutex_unlock(&machine.mmaps.mutex);
}

void km_guest_mmap_init(void)
{
   TAILQ_INIT(&machine.mmaps.free);
   TAILQ_INIT(&machine.mmaps.busy);
}

static void km_clean_list(km_mmap_list_t* list)
{
   km_mmap_reg_t *reg, *next;

   TAILQ_FOREACH_SAFE (reg, list, link, next) {
      TAILQ_REMOVE(list, reg, link);
      if (reg->filename != NULL) {
         free(reg->filename);
      }
      free(reg);
   }
}

void km_guest_mmap_fini(void)
{
   km_clean_list(&machine.mmaps.busy);
   km_clean_list(&machine.mmaps.free);
}

// on ubuntu and older kernels, this is not defined. We need symbol to check (and reject) flags
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Checks for stuff we do not support. Returns 0 on success, -errno on error
static inline int
mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   if (size >= GUEST_MEM_ZONE_SIZE_VA) {
      km_infox(KM_TRACE_MMAP, "*** size is too large");
      return -ENOMEM;
   }
   if ((flags & MAP_FIXED_NOREPLACE) != 0) {
      km_infox(KM_TRACE_MMAP, "*** MAP_FIXED_NOREPLACE is not supported");
      return -EINVAL;
   }
   if ((flags & MAP_FIXED) != 0 && addr == 0) {
      km_infox(KM_TRACE_MMAP, "*** bad fixed address");
      return -EPERM;
   }
   if ((flags & MAP_ANONYMOUS) == 0 && fd < 0) {
      km_infox(KM_TRACE_MMAP, "*** no fd and no MAP_ANONYMOUS flag");
      return -EBADF;
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
   if (machine.mmaps.recovery_mode != 0) {
      return 0;
   }
   if (left->filename != NULL) {
      if (right->filename == NULL || strcmp(left->filename, right->filename) != 0) {
         return 0;
      }
   } else if (right->filename != NULL) {
      return 0;
   }
   return (left->start + left->size == right->start && left->protection == right->protection &&
           left->flags == right->flags && left->km_flags.data32 == right->km_flags.data32);
}

/*
 * Concatenate 'reg' mmap with left and/or right neighbor in the 'list', if they have the
 * same properties. Remove from the list and free the excess neighbors.
 * Note: There are dependencies on the fact that reg as a pointer is intact, in other words the
 * concat keeps that structure and removes the *neighbors*.
 */
static inline void km_mmap_concat(km_mmap_reg_t* reg, km_mmap_list_t* list)
{
   km_mmap_reg_t* left = TAILQ_PREV(reg, km_mmap_list, link);
   km_mmap_reg_t* right = TAILQ_NEXT(reg, link);

   assert(reg != NULL && reg != left && reg != right);   // out of paranoia, check for cycles
   if (left != NULL && ok_to_concat(left, reg) == 1) {
      reg->start = left->start;
      reg->size += left->size;
      TAILQ_REMOVE(list, left, link);
      if (left->filename != NULL) {
         free(left->filename);
      }
      free(left);
   }
   if (right != NULL && ok_to_concat(reg, right) == 1) {
      reg->size += right->size;
      TAILQ_REMOVE(list, right, link);
      if (right->filename != NULL) {
         free(right->filename);
      }
      free(right);
   }
}

static void km_reg_make_clean(km_mmap_reg_t* reg)
{
   if (reg->protection != PROT_NONE && reg->km_flags.km_mmap_clean == 0 && reg->filename == NULL) {
      madvise(km_gva_to_kma_nocheck(reg->start), reg->size, MADV_DONTNEED);
      reg->km_flags.km_mmap_clean = 1;
      km_infox(KM_TRACE_MMAP, "zero km 0x%lx sz 0x%lx", reg->start, reg->size);
   }
}

// wrapper for mprotect() on a single mmap region.
static void km_mmap_mprotect_region(km_mmap_reg_t* reg)
{
   if (reg->km_flags.km_mmap_part_of_monitor == 0 &&
       mprotect(km_gva_to_kma_nocheck(reg->start), reg->size, protection_adjust(reg->protection)) != 0) {
      km_warn("Failed to mprotect addr 0x%lx sz 0x%lx prot 0x%x)", reg->start, reg->size, reg->protection);
   }
   km_reg_make_clean(reg);
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
   if (reg->filename != NULL) {
      free(reg->filename);
      reg->filename = NULL;
   }
   reg->offset = 0;
   reg->km_flags.km_mmap_clean = 0;
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
   if ((reg->flags & MAP_SHARED) != 0 || reg->filename != NULL) {
      km_kma_t start_kma = km_gva_to_kma(reg->start);
      int new_flags = (reg->flags & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANONYMOUS;
      void* tmp = mmap(start_kma, reg->size, reg->protection, new_flags | MAP_FIXED, -1, 0);
      if (tmp != start_kma) {
         km_warn("Couldn't convert existing mapping at kma %p (%s) to ANONYMOUS\n",
                 start_kma,
                 reg->filename != NULL ? reg->filename : "MAP_SHARED");
      } else {
         reg->flags = new_flags;
         reg->filename = NULL;
      }
   }
   km_mmap_insert_free(reg);
}

// Callback for when busy_range_apply needs to prepare one big empty region covering the range
static void km_mmap_region_clean_concat(km_mmap_reg_t* reg)
{
   reg->flags = -1;   // fake flag to guarantee reg will not concat with left and right neighbors
   km_reg_make_clean(reg);
   reg->offset = 0;
   if (reg->filename != NULL) {
      free(reg->filename);
      reg->filename = NULL;
   }
   km_mmap_concat(reg, &machine.mmaps.busy);
}

typedef void (*km_mmap_action)(km_mmap_reg_t*);
/*
 * Apply 'action(reg)' to all mmap regions completely within the (addr, addr+size-1) range in
 * 'busy' machine.mmaps. If needed splits the mmaps at the ends - new maps are also updated to
 * have protection 'prot'.
 *
 * Note that the behavior during the split depends on the action type, so we
 * check on the entry that action is expected.
 *
 * Returns 0 on success or -errno
 */
static int km_mmap_busy_range_apply(km_gva_t addr, size_t size, km_mmap_action action, int prot)
{
   km_mmap_reg_t *reg, *next;

   assert(action == km_mmap_mprotect || action == km_mmap_move_to_free ||
          action == km_mmap_region_clean_concat);
   TAILQ_FOREACH_SAFE (reg, &machine.mmaps.busy, link, next) {
      km_mmap_reg_t* extra;

      if (reg->start + reg->size <= addr) {
         continue;   // skip all to the left of addr
      }
      if (reg->start >= addr + size) {
         break;   // we passed the range and are done
      }
      if (reg->km_flags.km_mmap_monitor == 1 ||   // skip internal maps... not expected, so warn
          reg->km_flags.km_mmap_part_of_monitor == 1) {
         km_warnx("Range addr 0x%lx size 0x%lx conflicts with monitor region 0x%lx size 0x%lx",
                  addr,
                  size,
                  reg->start,
                  reg->size);
         continue;
      }
      if (reg->start < addr) {   // overlaps on the start
         if ((extra = calloc(1, sizeof(km_mmap_reg_t))) == NULL) {
            return -ENOMEM;
         }
         *extra = *reg;
         reg->size = addr - reg->start;   // left part, to keep in busy
         extra->start = addr;             // right part, to insert in busy
         extra->size -= reg->size;
         if (reg->filename != NULL) {
            extra->filename = strdup(reg->filename);
            extra->offset += reg->size;
         }
         km_mmap_insert_busy_after(reg, extra);
         next = extra;
         continue;
      }
      if (reg->start + reg->size > addr + size) {   // overlaps on the end
         if ((extra = calloc(1, sizeof(km_mmap_reg_t))) == NULL) {
            return -ENOMEM;
         }
         *extra = *reg;
         reg->size = addr + size - reg->start;   // left part , to insert in busy
         extra->start = addr + size;             // right part, to keep in busy
         extra->size -= reg->size;
         if (reg->filename != NULL) {
            extra->filename = strdup(reg->filename);
            extra->offset += reg->size;
         }
         km_mmap_insert_busy_after(reg, extra);   // fall through to process 'reg'
         next = TAILQ_END(&machine.mmaps.busy);   // no need to go further, end of addr + size
      }
      assert(reg->start >= addr && reg->start + reg->size <= addr + size);   // fully within the range
      reg->protection = prot;
      action(reg);
      // action() may concat maps and thus 'next' may need adjustment
      if (action != km_mmap_move_to_free && next != TAILQ_END(&machine.mmaps.busy)) {
         next = TAILQ_NEXT(reg, link);
      }
   }
   return 0;
}

/*
 * Checks if busy mmaps are contiguous from `addr' to `addr+size'.
 * Return 0 if they are, -1 if they are not.
 */
static int km_mmap_busy_check_contiguous(km_gva_t addr, size_t size)
{
   km_mmap_reg_t* reg;
   km_gva_t last_end = 0;

   TAILQ_FOREACH (reg, &machine.mmaps.busy, link) {
      if (reg->start + reg->size < addr) {
         continue;
      }
      if (reg->start >= addr + size) {
         if (last_end >= addr + size) {
            return 0;
         }
         return -1;   // gap at the end of the range
      }
      if ((last_end != 0 && last_end != reg->start) || (last_end == 0 && reg->start > addr) ||
          (reg->km_flags.km_mmap_monitor == 1) || reg->km_flags.km_mmap_part_of_monitor == 1) {
         return -1;   // gap in the beginning or before current reg, or stepped on monitor
      }
      last_end = reg->start + reg->size;
   }
   return last_end == 0 ? -1 : 0;
}

// Guest mprotect implementation. Params should be already checked and locks taken.
static int km_guest_mprotect_nolock(km_gva_t addr, size_t size, int prot)
{
   // mprotect allowed on memory under machine.brk
   if (addr + size <= machine.brk && addr >= GUEST_MEM_START_VA) {
      if (mprotect(km_gva_to_kma_nocheck(addr), size, protection_adjust(prot)) < 0) {
         return -errno;
      }
      return 0;
   }
   // Per mprotect(3) if there are un-mmaped pages in the area, error out with ENOMEM
   if (km_mmap_busy_check_contiguous(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "mprotect area not fully mapped");
      return -ENOMEM;
   }
   if (km_mmap_busy_range_apply(addr, size, km_mmap_mprotect, prot) != 0) {
      return -ENOMEM;
   }
   return 0;
}

static int km_guest_madvise_nolock(km_gva_t addr, size_t size, int advise)
{
   assert(advise == MADV_DONTNEED);
   if (km_mmap_busy_check_contiguous(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "madvise area not fully mapped");
      return -ENOMEM;
   }
   // TODO: assumes linear virt and phys mem. Do in loop by regs instead
   if (madvise(km_gva_to_kma_nocheck(addr), size, advise) != 0) {
      return -errno;
   }
   return 0;
}
static int km_guest_msync_nolock(km_gva_t addr, size_t size, int flag)
{
   if (km_mmap_busy_check_contiguous(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "msync area not fully mapped");
      return -ENOMEM;
   }
   // TODO: assumes linear virt and phys mem. Do in loop by regs instead
   if (msync(km_gva_to_kma_nocheck(addr), size, flag) != 0) {
      return -errno;
   }
   return 0;
}

// Returns pointer to a region containing <gva>, or NULL
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

// Guest munmap implementation. Params should be already checked and locks taken. Returns 0 or -errno
static int km_guest_munmap_nolock(km_gva_t addr, size_t size)
{
   return km_mmap_busy_range_apply(addr, size, km_mmap_move_to_free, PROT_NONE);
}

static int
km_mmap_change_region_sharing(km_mmap_reg_t* reg, int existing_flags, int desired_flags, int hostfd)
{
   if ((existing_flags & (MAP_PRIVATE | MAP_SHARED)) != (desired_flags & (MAP_PRIVATE | MAP_SHARED))) {
      // change from private to shared or vice versa
      km_kma_t start_kma = km_gva_to_kma(reg->start);
      void* tmp = mmap(start_kma, reg->size, reg->protection, MAP_FIXED | desired_flags, hostfd, 0);
      if (tmp != (void*)start_kma) {
         km_warn("Changing page 0x%lx from 0x%x to 0x%x failed, tmp %p",
                 reg->start,
                 existing_flags,
                 desired_flags,
                 tmp);
         return -errno;
      }
      reg->flags &= ~(MAP_PRIVATE | MAP_SHARED);
      reg->flags |= desired_flags & (MAP_PRIVATE | MAP_SHARED);
   }
   return 0;
}

/*
 * Add an new mmap region. Returns guest address for the region, or -errno
 * Address range is carved from free regions list, or allocated by moving machine.tbrk down
 */
static km_gva_t
km_mmap_add_region(km_gva_t gva, size_t size, int prot, int flags, int hostfd, mmap_allocation_type_e at)
{
   km_mmap_reg_t* reg;
   km_gva_t ret;
   int existing_flags;

   if ((reg = km_mmap_find_free(size)) != NULL) {   // found a 'free' mmap to accommodate the request
      assert(size <= reg->size);
      existing_flags = reg->flags;
      if (reg->size > size) {   // free mmap has extra room to be kept in 'free'
         km_mmap_reg_t* busy;
         if ((busy = calloc(1, sizeof(km_mmap_reg_t))) == NULL) {
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
      existing_flags = MAP_ANON | MAP_PRIVATE;
      if ((reg = calloc(1, sizeof(km_mmap_reg_t))) == NULL) {
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
   reg->km_flags.data32 = 0;
   reg->filename = NULL;
   reg->offset = 0;
   reg->km_flags.km_mmap_monitor = ((at == MMAP_ALLOC_MONITOR) ? 1 : 0);

   int rc = km_mmap_change_region_sharing(reg, existing_flags, flags, hostfd);
   if (rc < 0) {
      km_mmap_insert_free(reg);
      return rc;
   }

   ret = reg->start;   // reg->start can be modified if there is concat in insert_busy
   km_mmap_insert_busy(reg);
   return ret;
}

// Guest mmap implementation. Returns gva or -errno. Params should be already checked and locks taken...
static km_gva_t km_guest_mmap_nolock(
    km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset, mmap_allocation_type_e type)
{
   int ret;

   if ((flags & MAP_ANONYMOUS) == MAP_ANONYMOUS && fd != -1) {
      km_infox(KM_TRACE_MMAP, "Ignoring fd due to MAP_ANONYMOUS");
      fd = -1;   // per mmap(3) fd can be ignored if MAP_ANONYMOUS is set
   }
   // Now that we are under lock, make sure we can get correct fd
   int hostfd = -1;
   km_file_ops_t* ops;
   if (fd >= 0 && ((hostfd = km_fs_g2h_fd(fd, &ops)) < 0 || ops != NULL)) {
      km_infox(KM_TRACE_MMAP, "***failed to convert fd %d to host fd", fd);
      return -EBADF;
   }
   if ((flags & MAP_FIXED) == MAP_FIXED) {
      if (km_mmap_busy_check_contiguous(gva, size) != 0) {
         km_infox(KM_TRACE_MMAP, "Found not maped addresses in MAP_FIXED-requested range");
         return -EINVAL;
      }
   } else {   // ignore the hint and grab an address range
      gva = km_mmap_add_region(0, size, prot, flags, hostfd, type);
      if (km_syscall_ok(gva) < 0) {
         km_info(KM_TRACE_MMAP, "Failed to allocate new mmap region");
         return gva;
      }
   }
   if ((flags & MAP_FIXED) != MAP_FIXED && fd < 0) {   // no fd and no fixed - we are done
      return gva;
   }
   assert(type == MMAP_ALLOC_GUEST);   // further code is expected to apply to guest requests only

   // By now, a contigious region(s) should already exist, so let's ask system to mmap there
   km_kma_t kma = km_gva_to_kma(gva);
   assert(kma != NULL);
   if (mmap(kma, size, prot, flags | MAP_FIXED, hostfd, offset) != kma) {
      km_warn("System mmap failed. gva 0x%lx kma %p host fd %d off 0x%lx", gva, kma, hostfd, offset);
      return -errno;
   }
   // Now glue the underlying regions together
   if ((ret = km_mmap_busy_range_apply(gva, size, km_mmap_region_clean_concat, prot)) != 0) {
      errno = -ret;
      km_info(KM_TRACE_MMAP, "Failed to apply km_mmap_region_clean_concat");
      return ret;
   }

   // Update the formed busy map to correct info
   // TODO: change range_apply to return km_mmap_reg_t*, and drop the extra search here
   km_mmap_reg_t* reg = km_find_reg_nolock(gva);
   assert(reg != NULL);   // we just created it above
   assert(reg->start == gva && reg->size == size);
   reg->flags = flags & ~MAP_FIXED;   // we don't care it was FIXED once
   reg->protection = prot;
   if (reg->filename != NULL) {   // clean up old name, if there is one
      free(reg->filename);
      reg->filename = NULL;
   }
   char* filename = km_guestfd_name(NULL, fd);
   if (filename != NULL) {
      reg->filename = realpath(filename, NULL);
      reg->offset = offset;
   }
   km_mmap_concat(reg, &machine.mmaps.busy);
   return gva;
}

/*
 * Maps an address range in guest virtual space.
 *  - checks the params, takes the lock and calls the actual implementation
 *
 * Returns mapped addres on success, or -errno on failure.
 * -EINVAL if the args are not valid
 * -ENOMEM if fails to allocate memory
 */
static km_gva_t km_guest_mmap_impl(km_gva_t gva,
                                   size_t size,
                                   int prot,
                                   int flags,
                                   int fd,
                                   off_t offset,
                                   mmap_allocation_type_e allocation_type)
{
   km_gva_t ret;
   km_infox(KM_TRACE_MMAP,
            "0x%lx, 0x%lx, prot 0x%x flags 0x%x fd %d alloc 0x%x",
            gva,
            size,
            prot,
            flags,
            fd,
            allocation_type);
   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   size = roundup(size, KM_PAGE_SIZE);
   mmaps_lock();
   ret = km_guest_mmap_nolock(gva, size, prot, flags, fd, offset, allocation_type);
   mmaps_unlock();
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

/*
 * Add an entry to the busy mmap list for a part of the km address space
 * that has been added to the guest's address space.  This is useful
 * for memory we want in the guest code dumps and in the guest's
 * /proc/X/maps file.
 * This is for things like the vdso page or the parts of the guest
 * that are really extensions of the operating system.  Things like
 * the signal return trampoline, or the code stubs the idt points to.
 * The tag arg should be something like "[vvar]" or "[vdso]".
 * Returns:
 *   0 - success
 *   != 0 - failure
 */
int km_monitor_pages_in_guest(km_gva_t gva, size_t size, int protection, char* tag)
{
   km_mmap_reg_t* reg;
   char* tagcopy = NULL;

   km_infox(KM_TRACE_MMAP, "gva 0x%lx, sizeof %ld, protection 0x%x, tag %s", gva, size, protection, tag);
   if ((reg = calloc(1, sizeof(km_mmap_reg_t))) == NULL) {
      return ENOMEM;
   }
   if (tag != NULL && (tagcopy = strdup(tag)) == NULL) {
      free(reg);
      return ENOMEM;
   }
   reg->start = gva;
   reg->size = size;
   reg->flags = 0;
   reg->protection = protection;
   reg->km_flags.km_mmap_part_of_monitor = 1;
   reg->filename = tagcopy;
   reg->offset = 0;
   km_mmap_insert_busy(reg);
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
int km_guest_munmap(km_vcpu_t* vcpu, km_gva_t addr, size_t size)
{
   int ret;

   km_infox(KM_TRACE_MMAP, "munmap guest(0x%lx, 0x%lx)", addr, size);
   size = roundup(size, KM_PAGE_SIZE);
   if ((ret = mumap_check_params(addr, size)) != 0) {
      return ret;
   }
   /*
    * Detached pthreads unmap their stack before exiting. See __pthread_exit() calling
    * __unmap_self(). It's a problem for us as we need the stack to return result, and to accept
    * exit() call afterwards.
    * We check if we are indeed trying to unmap our own stack, and delay the unmap till the exit().
    */
   if (addr <= vcpu->stack_top && vcpu->stack_top < addr + size) {
      assert(vcpu->mapself_base == 0 && vcpu->mapself_size == 0);
      vcpu->mapself_base = addr;
      vcpu->mapself_size = size;
      km_infox(KM_TRACE_MMAP, "== delaying munmap, ret=%d", ret);
      return 0;
   }
   mmaps_lock();
   ret = km_guest_munmap_nolock(addr, size);
   mmaps_unlock();
   km_infox(KM_TRACE_MMAP, "== munmap ret=%d", ret);
   return ret;
}

void km_delayed_munmap(km_vcpu_t* vcpu)
{
   if (vcpu->mapself_size > 0) {
      mmaps_lock();
      int rc = km_guest_munmap_nolock(vcpu->mapself_base, vcpu->mapself_size);
      assert(rc == 0);
      mmaps_unlock();
      vcpu->mapself_base = 0;
      vcpu->mapself_size = 0;
   }
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

int km_guest_madvise(km_gva_t addr, size_t size, int advise)
{
   km_infox(KM_TRACE_MMAP, "madvise guest(0x%lx 0x%lx advise %x)", addr, size, advise);
   if (advise != MADV_DONTNEED) {
      return -EINVAL;
   }
   if (addr != rounddown(addr, KM_PAGE_SIZE) || (size = roundup(size, KM_PAGE_SIZE)) == 0) {
      return -EINVAL;
   }
   mmaps_lock();
   int ret = km_guest_madvise_nolock(addr, size, advise);
   mmaps_unlock();
   return ret;
}

int km_guest_msync(km_gva_t addr, size_t size, int flag)
{
   km_infox(KM_TRACE_MMAP, "msync guest(0x%lx 0x%lx advise %x)", addr, size, flag);
   if (addr != rounddown(addr, KM_PAGE_SIZE)) {
      return -EINVAL;
   }
   mmaps_lock();
   int ret = km_guest_msync_nolock(addr, size, flag);
   mmaps_unlock();
   return ret;
}

// Grows a mmap to size. old_addr is expected to be within ptr map. Returns new address or -errno
static km_gva_t
km_mremap_grow(km_mmap_reg_t* ptr, km_gva_t old_addr, size_t old_size, size_t size, int may_move)
{
   km_mmap_reg_t* next;
   size_t needed = size - old_size;

   assert(ptr->km_flags.km_mmap_monitor == 0 && ptr->km_flags.km_mmap_part_of_monitor == 0);
   assert(old_addr >= ptr->start && old_addr < ptr->start + ptr->size &&
          old_addr + old_size <= ptr->start + ptr->size && size > old_size);

   // If there is a large enough free slot after this, use it.
   if ((ptr->start + ptr->size == old_addr + old_size) && (next = TAILQ_NEXT(ptr, link)) != NULL &&
       next->start - (ptr->start + ptr->size) >= needed) {
      // Adjust free list and protections - cannot use km_guest_mmap here as it can grab a wrong area
      km_infox(KM_TRACE_MMAP, "mremap: reusing adjusted free map");
      km_mmap_reg_t* donor = km_mmap_find_address(&machine.mmaps.free, ptr->start + ptr->size);
      assert(donor != NULL && donor->size >= needed);   // MUST have free slot due to gap in busy
      ptr->size += needed;

      if (ptr->km_flags.km_mmap_clean == 1) {   // no km_init_init will be called on the whole ptr region
         km_mmap_reg_t extra = (km_mmap_reg_t){.start = donor->start,
                                               .size = needed,
                                               .protection = PROT_WRITE,
                                               .km_flags.km_mmap_clean = 0};
         km_reg_make_clean(&extra);
      }
      km_mmap_mprotect_region(ptr);
      if (donor->size == needed) {
         km_mmap_remove_free(donor);
         free(donor);
      } else {
         donor->start += needed;
         donor->size -= needed;
      }
      km_mmap_concat(ptr, &machine.mmaps.busy);
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
      km_err(1, "Failed to unmap after remapping");
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
   if (ptr->km_flags.km_mmap_monitor == 1 || ptr->km_flags.km_mmap_part_of_monitor == 1) {
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

static inline int
km_is_payload_gva_accessable(km_payload_t* payload, km_gva_t gva, size_t size, int prot)
{
   for (int i = 0; i < payload->km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &payload->km_phdr[i];
      if (gva < phdr->p_vaddr + payload->km_load_adjust ||
          gva + size > phdr->p_vaddr + payload->km_load_adjust + phdr->p_memsz) {
         continue;
      }
      int rprot = 0;
      if (phdr->p_flags & PF_R) {
         rprot |= PROT_READ;
      }
      if (phdr->p_flags & PF_W) {
         rprot |= PROT_WRITE;
      }
      if (phdr->p_flags & PF_X) {
         rprot |= PROT_EXEC;
      }
      if (rprot == 0 || (prot & rprot) != prot) {
         return 0;
      }
      return 1;
   }
   return 0;
}

/*
 * Determine is a GVA range is accessable at a particular
 * protection level.
 * @param gva   starting gva of range.
 * @param size  length of range
 * @param prot  protection flag(s) [PROT_READ, PROT_WRITE, PROT_EXEC]
 * @returns 1 if range is accessable, 0 if not.
 */
int km_is_gva_accessable(km_gva_t gva, size_t size, int prot)
{
   if (km_gva_to_kma(gva) == NULL) {
      return 0;
   }

   if (gva < machine.brk) {
      if (km_is_payload_gva_accessable(&km_guest, gva, size, prot)) {
         return 1;
      }
      if (km_is_payload_gva_accessable(&km_dynlinker, gva, size, prot)) {
         return 1;
      }
      return 0;
   }

   // Must be in mmap memory.
   km_mmap_reg_t* reg = NULL;
   int ret = 1;
   mmaps_lock();
   if ((reg = km_find_reg_nolock(gva)) == NULL) {
      ret = 0;
      goto done;
   }
   /*
    * The entire requested range must be described by a single region.
    * This is just laziness. Don't feel like checking against multiple
    * regions until there is a compelling reason.
    */
   if (gva + size > reg->start + reg->size) {
      km_errx(2, "range spanned mmap region gva:0x%lx size:0x%lx", gva, size);
   }
   if ((reg->protection & prot) != prot) {
      ret = 0;
      goto done;
   }
done:
   mmaps_unlock();
   return ret;
}

/*
 * Only set during snapshot recovery. Single threaded by defintion.
 */
void km_mmap_set_recovery_mode(int mode)
{
   machine.mmaps.recovery_mode = mode;
}

void km_mmap_set_filename(km_gva_t base, km_gva_t limit, char* filename)
{
   if (base < machine.tbrk) {
      return;
   }
   // TODO: filter out km_guest/vdso/etc.

   // Must be in mmap memory.
   km_mmap_reg_t* reg = NULL;
   if ((reg = km_find_reg_nolock(base)) == NULL) {
      km_errx(2, "cannot find region base=0x%lx", base);
   }
   if (reg->filename == NULL) {
      reg->filename = strdup(filename);
   }
}
