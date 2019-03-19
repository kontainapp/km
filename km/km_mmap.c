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

LIST_HEAD(km_mmap_list, km_mmap_reg);
typedef struct km_mmap_list km_mmap_list_t;

typedef struct km_mmap_reg {
   km_gva_t start;
   size_t size;
   int flags;
   int protection;
   LIST_ENTRY(km_mmap_reg) link;
} km_mmap_reg_t;

typedef struct km_mmap_cb {   // control block
   km_mmap_list_t free;       // list of free regions
   km_mmap_list_t busy;       // list of mapped regions
   pthread_mutex_t mutex;     // global map lock
} km_mmap_cb_t;

static km_mmap_cb_t mmaps = {
    .free = LIST_HEAD_INITIALIZER(free_head),
    .busy = LIST_HEAD_INITIALIZER(free_head),
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
   LIST_INIT(&mmaps.free);
   LIST_INIT(&mmaps.busy);
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
// TODO: sort free by size and return a smallest one
static km_mmap_reg_t* km_mmap_find_free(size_t size)
{
   km_mmap_reg_t* ptr;

   LIST_FOREACH (ptr, &mmaps.free, link) {
      if (ptr->size >= size) {
         return ptr;
      }
   }
   return NULL;
}

static inline km_gva_t mmap_end(km_mmap_reg_t* reg)
{
   return reg->start + reg->size;
}

// find a mmap segment containing the addr
static km_mmap_reg_t* km_mmap_find_busy(km_gva_t addr)
{
   km_mmap_reg_t* ptr;

   LIST_FOREACH (ptr, &mmaps.busy, link) {
      if (ptr->start <= addr && addr <= mmap_end(ptr)) {
         return ptr;
      }
   }
   return NULL;
}

km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   km_gva_t ret;
   km_mmap_reg_t* reg;

   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
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
      } else {   // full chunk is reused
         LIST_REMOVE(reg, link);
      }
      LIST_INSERT_HEAD(&mmaps.busy, carved, link);
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
   LIST_INSERT_HEAD(&mmaps.busy, reg, link);
   mmaps_unlock();
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
      LIST_INSERT_HEAD(&mmaps.busy, head, link);
   }
   if (tail != NULL) {
      memcpy(tail, reg, sizeof(*tail));
      tail->start = tail_start;
      tail->size = tail_size;
      LIST_INSERT_HEAD(&mmaps.busy, tail, link);
   }

   LIST_REMOVE(reg, link);   // remove from busy list
   if (reg->start == km_mem_tbrk(0)) {
      km_mem_tbrk(mmap_end(reg));
      free(reg);
   } else {
      LIST_INSERT_HEAD(&mmaps.free, reg, link);
   }
   mmaps_unlock();
   return 0;
}

// TODO - implement :-)
km_gva_t km_guest_mremap(
    km_gva_t old_address, size_t old_size, size_t new_size, int flags, ... /* void *new_address */)
{
   return -ENOTSUP;
}
