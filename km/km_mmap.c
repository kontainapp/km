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

   TAILQ_FOREACH (ptr, &mmaps.free, link) {
      if (ptr->size >= size) {
         return ptr;
      }
   }
   return NULL;
}

// calls mprotect on a single mmap region
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

// return 1 if ok to concat. Relies on 'free' mmaps all having 0 in protection and flags
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

// Insert a region into the BUSY MMAPS with proper protection. Expects 'reg' to be malloced
static inline void km_mmap_insert_busy(km_mmap_reg_t* reg)
{
   km_mmap_insert(reg, &mmaps.busy);
}

// Insert 'reg' into the FREE MMAPS with PROT_NONE, and compress maps/tbrk. Expects 'reg' to be malloced
static inline void km_mmap_insert_free(km_mmap_reg_t* reg)
{
   km_mmap_list_t* list = &mmaps.free;

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
   TAILQ_REMOVE(&mmaps.busy, reg, link);
}

// Remove an element from a list. In the list, this does not depend on list head
static inline void km_mmap_remove_free(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&mmaps.free, reg, link);
}

// moves existing mmap region from busy to free
static inline void km_mmap_move_to_free(km_mmap_reg_t* reg)
{
   km_mmap_remove_busy(reg);
   km_mmap_insert_free(reg);
}

// moves existing mmap region from busy to free
static inline void km_mmaps_move_to_busy(km_mmap_reg_t* reg)
{
   km_mmap_remove_free(reg);
   km_mmap_insert_busy(reg);
}

static inline void km_mmap_busy_collapse(void)
{
   km_mmap_reg_t *reg, *next, *last = NULL;
   TAILQ_FOREACH_SAFE (reg, &mmaps.busy, link, next) {
      if (last != NULL && last->start + last->size == reg->start && last->flags == reg->flags &&
          last->protection == reg->protection) {
         last->size += reg->size;
         TAILQ_REMOVE(&mmaps.busy, reg, link);
         free(reg);
      }
      last = reg;
   }
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
km_gva_t km_guest_mmap(km_gva_t gva, size_t size, int prot, int flags, int fd, off_t offset)
{
   km_gva_t ret;
   km_mmap_reg_t* reg;

   km_infox(KM_TRACE_MMAP, "mmap guest(0x%lx, 0x%lx, prot 0x%x flags 0x%x)", gva, size, prot, flags);
   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   mmaps_lock();
   if ((reg = km_mmap_find_free(size)) != NULL) {   // found a 'free' mmap to accommodate the request
      if (reg->size > size) {                       // free mmap has extra room to be kept in 'free'
         km_mmap_reg_t* busy;
         if ((busy = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            mmaps_unlock();
            return -ENOMEM;
         }
         memcpy(busy, reg, sizeof(km_mmap_reg_t));
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
         mmaps_unlock();
         return -ENOMEM;
      }
      km_gva_t want = machine.tbrk - size;
      if ((ret = km_mem_tbrk(want)) != want) {
         mmaps_unlock();
         free(reg);
         return ret;
      }
      reg->start = ret;   //  place requested mmap region in the newly allocated memory
      reg->size = size;
   }
   reg->flags = flags;
   reg->protection = prot;
   km_mmap_insert_busy(reg);
   km_gva_t new_map_start = reg->start;   // note: 'reg' can be modified during collapse
   km_mmap_busy_collapse();
   mmaps_unlock();
   return new_map_start;
}

typedef void (*km_mmap_action)(km_mmap_reg_t* reg);   // action type to apply to mmaps within the range
/*
 * Apply 'action(reg)' to all mmap regions completely within the (addr, addr+size-1) range in 'busy'
 * mmaps. If needed splits the mmaps at the ends - new maps are also updated to have protection
 * 'prot',
 *
 * Returns 0 on success or -errno
 */
static int km_mmap_busy_range_apply(km_gva_t addr, size_t size, km_mmap_action action, int prot)
{
   km_mmap_reg_t *reg, *next, *extra;

   TAILQ_FOREACH_SAFE (reg, &mmaps.busy, link, next) {
      if (reg->start + reg->size <= addr) {
         continue;   // skip all to the left of addr
      }
      if (reg->start >= addr + size) {
         break;   // we passed the range and are done
      }
      if (reg->start < addr) {   // overlaps on the start
         if ((extra = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            mmaps_unlock();
            return -ENOMEM;
         }
         memcpy(extra, reg, sizeof(*reg));
         reg->size = addr - reg->start;   // left part
         extra->start = addr;             // right part
         extra->size -= reg->size;
         km_mmap_insert_busy(extra);   // TODO - allow 'insert_busy_after'
         next = extra;                 // handle the freshly inserted mmap
         continue;
      }
      if (reg->start + reg->size > addr + size) {   // overlaps on the end
         if ((extra = malloc(sizeof(km_mmap_reg_t))) == NULL) {
            mmaps_unlock();
            return -ENOMEM;
         }
         memcpy(extra, reg, sizeof(*reg));
         extra->size = addr + size - extra->start;   // left part
         reg->start = addr + size;                   // right part
         reg->size -= extra->size;
         km_mmap_insert_busy(extra);
         next = extra;   // handle the freshly inserted mmap
         continue;
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
int km_guest_munmap(km_gva_t addr, size_t size)
{
   int ret;

   km_infox(KM_TRACE_MMAP, "munmap guest(0x%lx, 0x%lx)", addr, size);
   size = roundup(size, KM_PAGE_SIZE);
   if ((ret = mumap_check_params(addr, size)) != 0) {
      return ret;
   }
   mmaps_lock();
   ret = km_mmap_busy_range_apply(addr, size, km_mmap_move_to_free, PROT_NONE);
   mmaps_unlock();
   return ret;
}

/*
 * Checks if busy mmaps are contiguous from `addr' to `addr+size'.
 * Return 0 if they are, -1 if they are not.
 */
static int km_mmap_busy_check_contigious(km_gva_t addr, size_t size)
{
   km_mmap_reg_t* reg;
   km_gva_t last_end = 0;

   TAILQ_FOREACH (reg, &mmaps.busy, link) {
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
   // Per mprotect(3) if there are un-mmaped pages in the area, error out with ENOMEM
   if (km_mmap_busy_check_contigious(addr, size) != 0) {
      km_infox(KM_TRACE_MMAP, "mprotect area not fully mapped");
      mmaps_unlock();
      return -ENOMEM;
   }
   if (km_mmap_busy_range_apply(addr, size, km_mmap_mprotect_region, prot) != 0) {
      mmaps_unlock();
      return -ENOMEM;
   }
   km_mmap_busy_collapse();
   mmaps_unlock();
   return 0;
}

// TODO - implement
km_gva_t
km_guest_mremap(km_gva_t old_addr, size_t old_size, size_t size, int flags, ... /* void *new_address */)
{
   km_infox(KM_TRACE_MMAP, "mremap(0x%lx, 0x%lx, size 0x%lx)", old_addr, old_size, size);
   return -ENOTSUP;
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
   TAILQ_FOREACH (ptr, &mmaps.busy, link) {
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
   TAILQ_FOREACH (ptr, &mmaps.busy, link) {
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
   TAILQ_FOREACH (ptr, &mmaps.busy, link) {
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