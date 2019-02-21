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
   km_gva_t min_used;         // lowest address used
} km_mmap_cb_t;

static km_mmap_cb_t mmaps = {
    .free = LIST_HEAD_INITIALIZER(free_head),
    .busy = LIST_HEAD_INITIALIZER(free_head),
};

void km_guest_mmap_init(void)
{
   LIST_INIT(&mmaps.free);
   LIST_INIT(&mmaps.busy);
   mmaps.min_used = machine.tbrk;
}

static int mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   // block all stuff we do not support yet
   if (addr != 0 || fd != -1 || offset != 0 || flags & MAP_FIXED) {
      return -EINVAL;
   }
   if (size > machine.guest_max_physmem - machine.brk) {
      return -ENOMEM;
   }
   return 0;
}

// find the first free mmap with enough space, if any
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

#define MMAP_END(ptr) ((ptr)->start + (ptr)->size)

// find a mmap segment containing the addr
static km_mmap_reg_t* km_mmap_find_busy(km_gva_t addr)
{
   km_mmap_reg_t* ptr;

   LIST_FOREACH (ptr, &mmaps.busy, link) {
      if (ptr->start <= addr && addr <= MMAP_END(ptr)) {
         return ptr;
      }
   }
   return NULL;
}

km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   int ret;
   km_mmap_reg_t* reg;

   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   // TODO: check flags
   if ((reg = km_mmap_find_free(size)) != NULL) {
      // found a free chunk, carve from it
      km_mmap_reg_t* carved = reg;
      if (MMAP_END(reg) > gva + size) {   // keep extra space in free list
         if ((carved = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            return -ENOMEM;
         }
         memcpy(carved, reg, sizeof(km_mmap_reg_t));
         carved->size = size;
         reg->start += size;
         reg->size -= size;
         LIST_INSERT_HEAD(&mmaps.free, carved, link);
      }
      LIST_REMOVE(reg, link);
      return carved->start;
   }
   // nothing useful in the free list, get fresh memory by moving tbrk down
   while (size > mmaps.min_used - machine.tbrk) {
      if ((ret = km_mmap_extend()) != 0) {
         return ret;   // failed to extend mmap region downward
      }
   }
   if ((reg = malloc(sizeof(km_mmap_reg_t))) == NULL) {
      return -ENOMEM;
   }
   //  place requested mmap region in the newly allocated memory
   mmaps.min_used -= size;
   reg->start = mmaps.min_used;
   reg->flags = flags;
   reg->protection = prot;
   reg->size = size;
   LIST_INSERT_HEAD(&mmaps.busy, reg, link);
   return reg->start;
}

/*
 * Un-maps an  address from guest virtual space.
 *
 * TODO - unmap partial regions and/or manage holes. For now un-maps only FULL regions previously
 * mapped.
 * TODO - manipulate km-level map/unmap when adding/remove to free/busy lists (since we cannot do it
 * in pml4 due to coarse granularity)
 *
 * Returns 0 on success. -ENOTSUP if the part of the FULL requested region is not mapped
 */
int km_guest_munmap(km_gva_t addr, size_t size)
{
   km_mmap_reg_t* reg;
   if ((reg = km_mmap_find_busy(addr)) == NULL) {
      return -EINVAL;
   }
   if (addr == reg->start && size == reg->size) {
      LIST_REMOVE(reg, link);
      LIST_INSERT_HEAD(&mmaps.free, reg, link);
      return 0;
   }
   // TODO: Carve a hole in existing maps (ignore unmapped space, per spec), and return 0
   return -ENOTSUP;
}

// TODO
km_gva_t km_guest_mremap(
    void* old_address, size_t old_size, size_t new_size, int flags, ... /* void *new_address */)
{
   return -ENOTSUP;
}
