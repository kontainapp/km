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
 *
 * Support for payload mmap() and related API
 */
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_mem.h"

TAILQ_HEAD(km_mmap_list, km_mmap_reg);
typedef struct km_mmap_list km_mmap_list_t;

typedef struct km_mmap_reg {
   km_gva_t start;
   size_t size;
   int flags;
   int protection;
   TAILQ_ENTRY(km_mmap_reg) link;
} km_mmap_reg_t;

typedef struct km_mmap_cb {   // control block
   km_mmap_list_t free;       // list of free regions
   km_mmap_list_t busy;       // list of mapped regions
   pthread_mutex_t mutex;     // global map lock
} km_mmap_cb_t;

static km_mmap_cb_t mmaps = {
    .free = TAILQ_HEAD_INITIALIZER(mmaps.free),
    .busy = TAILQ_HEAD_INITIALIZER(mmaps.busy),
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

static inline void mmaps_lock(void)
{
   pthread_mutex_lock(&mmaps.mutex);
}

static inline void mmaps_unlock(void)
{
   pthread_mutex_unlock(&mmaps.mutex);
}

void km_guest_mmap_init(void)
{
   TAILQ_INIT(&mmaps.free);
   TAILQ_INIT(&mmaps.busy);
}

static inline int
mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   // block all stuff we do not support yet
   if (addr != 0 || fd != -1 || offset != 0 || flags & MAP_FIXED) {
      return -EINVAL;
   }
   return 0;
}

// find any free mmap larger or equal to 'size'
static km_mmap_reg_t* km_mmap_find_free(size_t size)
{
   km_mmap_reg_t* ptr;

   TAILQ_FOREACH (ptr, &mmaps.free, link) {
      if (ptr->size >= size) {
         return ptr;
      }
   }
   return NULL;
}

// A helper to return the 'end' , i.e. the address of the first bite *after* the region
static inline km_gva_t mmap_end(km_mmap_reg_t* reg)
{
   return reg->start + reg->size;
}

// find a mmap segment containing the addr in the sorted busy list
static km_mmap_reg_t* km_mmap_find_busy(km_gva_t addr)
{
   km_mmap_reg_t* ptr;

   TAILQ_FOREACH (ptr, &mmaps.busy, link) {
      if (ptr->start <= addr && addr < mmap_end(ptr)) {
         return ptr;
      }
   }
   return NULL;
}

/*
 * Try to glue with left and/or right region from 'reg'.
 * 'Reg' is assumed to be in free list already.
 * When the regions are concat together, the excess ones are freed and removed fron the list
 */
static inline void km_mmap_concat_free(km_mmap_reg_t* reg)
{
   km_mmap_list_t* list = &mmaps.free;
   km_mmap_reg_t* left = TAILQ_PREV(reg, km_mmap_list, link);
   km_mmap_reg_t* right = TAILQ_NEXT(reg, link);

   if (left != NULL && mmap_end(left) == reg->start) {
      left->size += reg->size;
      if (right != NULL && mmap_end(reg) == right->start) {
         left->size += right->size;
         TAILQ_REMOVE(list, right, link);
         free(right);
      }
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   } else if (right != NULL && mmap_end(reg) == right->start) {
      right->start = reg->start;
      right->size += reg->size;
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   }

   // adjust tbrk() if needed
   if ((reg = TAILQ_FIRST(list))->start == km_mem_tbrk(0)) {
      km_mem_tbrk(mmap_end(reg));
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   }
}

/*
 * Inserts region into the list sorted by reg->start.
 * Also compresses the list/adjusts tbrk for free list
 * 'busy' == 1 for insert into BUSY list, 0 for insert into FREE list.
 *
 * TODO: Could (and should) be changed to avoid multiple scans
 * by passing optional location where to insert (since it's usually known from prev. search).
 * Leaving it nonoptimal for now, in assumption the map lists are very short
 *
 */
static inline void km_mmaps_insert(km_mmap_reg_t* reg, int busy)
{
   km_mmap_list_t* list = busy ? &mmaps.busy : &mmaps.free;
   km_mmap_reg_t* ptr;

   if (TAILQ_EMPTY(list)) {
      TAILQ_INSERT_HEAD(list, reg, link);
      return;
   }

   TAILQ_FOREACH (ptr, list, link) {   // find the map to the right of the 'reg'
      if (ptr->start > reg->start) {
         assert(ptr->start >= mmap_end(reg));
         break;
      }
      // double check that there are no overlaps (we don't support overlapping mmaps)
      assert(ptr->start < reg->start && mmap_end(ptr) <= reg->start);
   }

   if (ptr == TAILQ_END(list)) {
      TAILQ_INSERT_TAIL(list, reg, link);
   } else {
      TAILQ_INSERT_BEFORE(ptr, reg, link);
   }

   if (busy == 0) {
      km_mmap_concat_free(reg);
   }
   // Nothing else to do for busy list. TODO: try to consolidate if flags/prot match
}

// Insert a region into the BUSY MMAPS list. Expects 'reg' to be malloced
static inline void km_mmaps_insert_busy(km_mmap_reg_t* reg)
{
   km_mmaps_insert(reg, 1);
}

// Insert a region into the FREE MMAPS list, and compress maps/tbrk. Expects 'reg' to be malloced
static inline void km_mmaps_insert_free(km_mmap_reg_t* reg)
{
   km_mmaps_insert(reg, 0);
}

// Remove an element from a list.  In the list, this does not depend on list  head
static inline void km_mmaps_remove_free(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&mmaps.free, reg, link);
}
static inline void km_mmaps_remove_busy(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&mmaps.busy, reg, link);
}

km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   km_gva_t ret;
   km_mmap_reg_t* reg;

   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   km_infox("mmap entry. tbrk=0x%lx", machine.tbrk);
   mmaps_lock();
   // TODO: check flags
   if ((reg = km_mmap_find_free(size)) != NULL) {
      // found a free chunk with enough room, carve requested space from it
      km_mmap_reg_t* carved = reg;
      if (reg->size > size) {   // keep extra space in free list
         if ((carved = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            mmaps_unlock();
            return -ENOMEM;
         }
         memcpy(carved, reg, sizeof(km_mmap_reg_t));
         carved->size = size;
         reg->start += size;
         reg->size -= size;
      } else {   // full chunk is reused -
         km_mmaps_remove_free(reg);
      }
      km_mmaps_insert_busy(carved);
      mmaps_unlock();
      return carved->start;
   }

   // nothing useful in the free list, get fresh memory by moving tbrk down
   if ((reg = malloc(sizeof(km_mmap_reg_t))) == NULL) {
      mmaps_unlock();
      return -ENOMEM;
   }
   if (km_syscall_ok(ret = km_mem_tbrk(machine.tbrk - size)) < 0) {
      mmaps_unlock();
      free(reg);
      return ret;
   }
   reg->start = ret;   //  place requested mmap region in the newly allocated memory
   reg->flags = flags;
   reg->protection = prot;
   reg->size = size;
   km_mmaps_insert_busy(reg);
   mmaps_unlock();
   km_infox("mmap exit. tbrk=0x%lx", machine.tbrk);
   return reg->start;
}

/*
 * Un-maps an  address from guest virtual space.
 *
 * TODO - manage unmap() which cover non-mapped regions, or more than 1 region.
 * For now un-maps only full or part of a region previously mapped.
 *
 * TODO - manipulate km-level mprotect() to match guest-mapped areas, to prevent access to not
 * mapped memory in the guest
 *
 * Returns 0 on success.
 * -EINVAL if the part of the FULL requested region is not mapped
 * -ENOMEM if fails to allocate memory for control structures
 */
int km_guest_munmap(km_gva_t addr, size_t size)
{
   km_mmap_reg_t *reg = NULL, *head = NULL, *tail = NULL;
   km_gva_t head_start, tail_start;
   size_t head_size, tail_size;

   km_infox("munmap entry. tbrk=0x%lx", machine.tbrk);
   mmaps_lock();
   if (size == 0 || (reg = km_mmap_find_busy(addr)) == NULL || (addr + size > mmap_end(reg))) {
      mmaps_unlock();
      return -EINVAL;   // unmap start and end have to be in a single mapped region, for now
   }
   // Calculate head and tail regions (possibly of 0 size). If not empty, they would stay in busy list.
   head_start = reg->start;
   head_size = addr - head_start;
   tail_start = addr + size;
   tail_size = mmap_end(reg) - tail_start;
   if (head_size > 0 && (head = malloc(sizeof(km_mmap_reg_t))) == NULL) {
      mmaps_unlock();
      return -ENOMEM;
   }
   if (tail_size > 0 && ((tail = malloc(sizeof(km_mmap_reg_t))) == NULL)) {
      if (head != NULL) {
         free(head);
      }
      mmaps_unlock();
      return -ENOMEM;
   }
   reg->start += head_size;
   reg->size -= (head_size + tail_size);
   if (head != NULL) {
      memcpy(head, reg, sizeof(*head));
      head->start = head_start;
      head->size = head_size;
      km_mmaps_insert_busy(head);
   }
   if (tail != NULL) {
      memcpy(tail, reg, sizeof(*tail));
      tail->start = tail_start;
      tail->size = tail_size;
      km_mmaps_insert_busy(tail);
   }

   km_mmaps_remove_busy(reg);   // move from busy to free list. No need to free reg, it will be reused
   km_mmaps_insert_free(reg);
   mmaps_unlock();
   km_infox("munmap exit. tbrk=0x%lx", machine.tbrk);
   return 0;
}

// TODO - implement :-)
km_gva_t
km_guest_mremap(km_gva_t old_addr, size_t old_size, size_t size, int flags, ... /* void *new_address */)
{
   return -ENOTSUP;
}
