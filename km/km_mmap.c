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

static inline int
mmap_check_params(km_gva_t addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   // block all stuff we do not support yet.
   // TODO: We don't support address hints either, we simply ignore it for now.
   if (fd != -1 || offset != 0 || flags & MAP_FIXED) {
      return -EINVAL;
   }
   if (size % KM_PAGE_SIZE != 0) {
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

// find a mmap segment containing the addr in the sorted busy list
static km_mmap_reg_t* km_mmap_find_busy(km_gva_t addr)
{
   km_mmap_reg_t* ptr;

   TAILQ_FOREACH (ptr, &mmaps.busy, link) {
      if (ptr->start <= addr && addr < ptr->start + ptr->size) {
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

   if (left != NULL && (left->start + left->size == reg->start)) {
      left->size += reg->size;
      if (right != NULL && (reg->start + reg->size == right->start)) {
         left->size += right->size;
         TAILQ_REMOVE(list, right, link);
         free(right);
      }
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   } else if (right != NULL && (reg->start + reg->size == right->start)) {
      right->start = reg->start;
      right->size += reg->size;
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   }

   // adjust tbrk() if needed
   if ((reg = TAILQ_FIRST(list))->start == km_mem_tbrk(0)) {
      km_mem_tbrk(reg->start + reg->size);
      TAILQ_REMOVE(list, reg, link);
      free(reg);
   }
}

/*
 * Inserts region into the list sorted by reg->start.
 * Also compresses the list/adjusts tbrk for free list
 * 'busy' == 1 for insert into BUSY list, 0 for insert into FREE list.
 */
static inline void km_mmaps_insert(km_mmap_reg_t* reg, int busy)
{
   km_mmap_list_t* list = busy ? &mmaps.busy : &mmaps.free;
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

   if (busy == 0) {
      if (mprotect(km_gva_to_kma_nocheck(reg->start), reg->size, PROT_NONE) != 0) {
         warn("Failed to mprotect addr 0x%lx sz 0x%lx prot NONE)", reg->start, reg->size);
      }
      km_mmap_concat_free(reg);
   } else {
      if (mprotect(km_gva_to_kma_nocheck(reg->start), reg->size, reg->protection) != 0) {
         warn("Failed to mprotect addr 0x%lx sz 0x%lx prot 0x%x)", reg->start, reg->size, reg->protection);
      }
      // TODO: try to consolidate busy list if flags/prot match
   }
   km_infox(KM_TRACE_MEM,
            "mprotect busy(0x%lx, 0x%lx, flag 0x%x)",
            reg->start,
            reg->size,
            busy ? reg->protection : PROT_NONE);
}

// Insert a region into the BUSY MMAPS list. Expects 'reg' to be malloced
static inline void km_mmaps_insert_busy(km_mmap_reg_t* reg)
{
   km_mmaps_insert(reg, 1);
}

// Insert a region into the FREE MMAPS list, and compress maps/tbrk. Expects 'reg' to be malloced
static inline void km_mmap_insert_free(km_mmap_reg_t* reg)
{
   km_mmaps_insert(reg, 0);
}

// Remove an element from a list. In the list, this does not depend on list head
static inline void km_mmaps_remove_free(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&mmaps.free, reg, link);
}

static inline void km_mmap_remove_busy(km_mmap_reg_t* reg)
{
   TAILQ_REMOVE(&mmaps.busy, reg, link);
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
   km_infox(KM_TRACE_MEM, "mmap guest(0x%lx, 0x%lx, prot 0x%x flags 0x%x)", gva, size, prot, flags);
   if ((ret = mmap_check_params(gva, size, prot, flags, fd, offset)) != 0) {
      return ret;
   }
   mmaps_lock();
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
      carved->protection = prot;
      km_mmaps_insert_busy(carved);
      mmaps_unlock();
      return carved->start;
   }

   // nothing useful in the free list, get fresh memory by moving tbrk down
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
   reg->flags = flags;
   reg->protection = prot;
   reg->size = size;
   km_mmaps_insert_busy(reg);
   mmaps_unlock();
   return reg->start;
}

/*
 * Un-maps an address range from guest virtual space.
 *
 * TODO - manage unmap() which cover non-mapped regions, or more than 1 region.
 * For now un-maps only full or part of a region previously mapped.
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
   km_infox(KM_TRACE_MEM, "munmap guest(0x%lx, 0x%lx)", addr, size);

   size = roundup(size, KM_PAGE_SIZE);
   mmaps_lock();
   if (size == 0 || (reg = km_mmap_find_busy(addr)) == NULL) {
      mmaps_unlock();
      warnx("unmap: EINVAL on params");
      return -EINVAL;   // unmap start and end have to be in a single mapped region, for now
   }
   // || (addr + size > reg->start + reg->size)
   // Calculate head and tail regions (possibly of 0 size). If not empty, they would stay in busy list.
   head_start = reg->start;
   head_size = addr - head_start;
   tail_start = addr + size;
   tail_size = reg->start + reg->size - tail_start;
   if (head_size > 0 && (head = malloc(sizeof(km_mmap_reg_t))) == NULL) {
      mmaps_unlock();
      warnx("unmap: ENOMEM1");
      return -ENOMEM;
   }
   if (tail_size > 0 && ((tail = malloc(sizeof(km_mmap_reg_t))) == NULL)) {
      if (head != NULL) {
         free(head);
      }
      mmaps_unlock();
      warnx("unmap: ENOMEM2");
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

   km_mmap_remove_busy(reg);   // move from busy to free list. No need to free reg, it will be reused
   km_mmap_insert_free(reg);
   mmaps_unlock();
   return 0;
}

/*
 * Checks if mmaps are contiguous from `reg' region to `end' region.
 * Return 1 if they are, 0 if they are not
 */
static int km_mmap_is_contigious(km_mmap_reg_t* reg, km_mmap_reg_t* end)
{
   if (reg == end) {
      return 1;
   }
   assert(reg != NULL && end != NULL && reg->start < end->start);
   for (km_mmap_reg_t* next = TAILQ_NEXT(reg, link); reg != NULL && reg->start <= end->start;
        reg = next, next = TAILQ_NEXT(reg, link)) {
      if (reg->start + reg->size != next->start) {
         return 0;   // found a gap in mem coverage
      }
   }
   return 1;
}

/*
 * returns 0 (success) or -errno
 */
int km_guest_mprotect(km_gva_t addr, size_t size, int prot)
{
   km_mmap_reg_t *reg = NULL, *head = NULL, *tail = NULL;

   km_infox(KM_TRACE_MEM, "mprotect guest(0x%lx 0x%lx prot %x)", addr, size, prot);
   if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_GROWSDOWN | PROT_GROWSUP)) != 0) {
      return -EINVAL;
   }
   if (addr != rounddown(addr, KM_PAGE_SIZE) || (size = roundup(size, KM_PAGE_SIZE)) == 0) {
      return -EINVAL;
   }
   mmaps_lock();
   if ((head = km_mmap_find_busy(addr)) == NULL ||
       (tail = km_mmap_find_busy(addr + size - 1)) == NULL || km_mmap_is_contigious(head, tail) == 0) {
      warnx("not contigious");
      mmaps_unlock();
      return -EINVAL;
   }
   // Split first mmap, if needed.
   assert(addr >= head->start);
   if (addr > head->start) {
      if ((reg = malloc(sizeof(km_mmap_reg_t))) == NULL) {
         mmaps_unlock();
         return -ENOMEM;
      }
      memcpy(reg, head, sizeof(*head));
      reg->size = addr - head->start;
      head->start = addr;
      head->size -= reg->size;
      km_mmaps_insert_busy(reg);
   }
   // split the last mmap, if needed
   if (tail->start + tail->size > addr + size) {
      if ((reg = malloc(sizeof(km_mmap_reg_t))) == NULL) {
         mmaps_unlock();
         return -ENOMEM;
      }
      memcpy(reg, tail, sizeof(*tail));
      reg->start = addr + size;
      reg->size = tail->start + tail->size - reg->start;
      tail->size -= reg->size;
      km_mmaps_insert_busy(reg);
   }
   // now scan the maps and change mprotect on them
   for (reg = head; reg != NULL; reg = TAILQ_NEXT(reg, link)) {
      km_infox(KM_TRACE_MEM, "mprotect guest: setting 0x%lx,0x%lx -> 0x%x", reg->start, reg->size, prot);
      if (mprotect(km_gva_to_kma(reg->start), reg->size, prot) != 0) {
         warn("mprotect guest: failed for 0x%lx,0x%lx -> 0x%x", reg->start, reg->size, prot);
      }
      reg->protection = prot;
      if (reg == tail) {
         break;
      }
   }
   assert(reg == tail);
   mmaps_unlock();
   return 0;
}

// TODO - implement
km_gva_t
km_guest_mremap(km_gva_t old_addr, size_t old_size, size_t size, int flags, ... /* void *new_address */)
{
   km_infox(KM_TRACE_MEM, "mremap(0x%lx, 0x%lx, size 0x%lx)", old_addr, old_size, size);
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