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
 * Useful headers for mmap tests
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "km_mem.h"
#include "km_unittest.h"

extern int main(int argc, char** argv);
#define KM_PAYLOAD() ((uint64_t)&main < 4 * MIB)   // in KM, we load from 2Mb, In Linux, from 4MB
#define ASSERT_MMAPS_COUNT(_c)                                                                     \
   if (KM_PAYLOAD() == 1) {                                                                        \
      get_maps(greatest_get_verbosity());                                                          \
      ASSERT_EQ_FMT((_c), info->ntotal, "%d");                                                     \
   }

// Type of operation invoked by a single line in test tables
typedef enum {
   TYPE_MMAP,   // see mmap_test_t below
   TYPE_MUNMAP,
   TYPE_MPROTECT,
   TYPE_MREMAP,   // for remapped memory, PROT_WRITE should have been set on mmmap (test will write!)
   TYPE_READ,     // do a read to <offset, offset+size> from last mmap or mremap
   TYPE_WRITE,    // do a write to <offset, offset+size> from last mmap (use 'prot' for data)
   TYPE_USE_MREMAP_ADDR,   // in further tests, base all on address returned by last mremap (not mmap)
   TYPE_MADVISE,
} call_type_t;

typedef struct mmap_test {
   int line;           // line # in the src file
   char* info;         // string to help identify the test. NULL indicates the end of table
   call_type_t type;   // Operation type
   uint64_t offset;    // for mmap - address. For others - offset from the last mmap result
   size_t size;        // size for the operation (for mmap - 'old_size')
   uint64_t prot;      // For mmap - protection.
   // For WRITE - data to write
   // For READ - data expected (0 means ignore)
   // For mremap - 'new_size'
   int flags;      // flags to pass to mmap() or mremap()
   int expected;   // 0 if success is expected. Expected signal (for read/write) or errno
   int advise;
} mmap_test_t;

#define OK 0   // expected good result

#define UNUSED __attribute__((unused))
static const char* errno_fmt UNUSED = "errno 0x%x";   // format for offsets/types error msg
static const char* ret_fmt UNUSED = "ret 0x%x";       // format for offsets/types error msg
// just to type less going forward
static const int flags = (MAP_PRIVATE | MAP_ANONYMOUS);

static const int MAX_MAPS = 4096;
static km_ut_get_mmaps_t* info;

// human readable print for addresses and sizes
static char* out_sz(uint64_t val) UNUSED;
static char* out_sz(uint64_t val)
{
   char* buf = malloc(64);   // yeah. leak it
   if (val < 1 * MIB) {
      sprintf(buf, "%ld", val);
      return buf;
   }
   if (val < 1 * GIB) {
      sprintf(buf, "%ldmb", val / MIB);
      return buf;
   }
   sprintf(buf, "%ldgb + %ldmb", val / GIB, val / MIB - (val / GIB) * 1024);
   return buf;
}

static int get_maps(int verbose) UNUSED;
static int get_maps(int verbose)
{
   int ret;
   if (KM_PAYLOAD() != 1) {
      if (verbose == 1) {
         printf("not running in payload, skipping get_maps (%s)\n", __FUNCTION__);
      }
      return -1;
   }
   if (info == NULL) {
      info = calloc(1, sizeof(km_ut_get_mmaps_t) + MAX_MAPS * sizeof(km_mmap_reg_t));
      assert(info != NULL);
   }
   info->ntotal = MAX_MAPS;
   if ((ret = syscall(HC_km_unittest, KM_UT_GET_MMAPS_INFO, info)) == -EAGAIN) {
      printf("Wow, Km reported too many maps: %d\n", info->ntotal);
      return -1;
   }
   if (ret == -ENOTSUP) {
      printf("HC_km_unittest returned NOT SUPPORTED\n");
      return 0;
   }
   if (info->ntotal < 2) {   // we always have at least 2 mmaps: stack + IDT/GDT
      printf("Wow, Km reported too few maps: %d\n", info->ntotal);
      return -1;
   }
   size_t old_end = info->maps[0].start;
   if (verbose) {
      printf("maps: total %d free %d busy %d\n", info->ntotal, info->nfree, info->ntotal - info->nfree);
   }
   for (km_mmap_reg_t* reg = info->maps; reg < info->maps + info->ntotal; reg++) {
      char* type = (reg < info->maps + info->nfree ? "free" : "busy");
      if (reg == info->maps + info->nfree) {   // reset distance on 'busy' list stat
         old_end = reg->start;
      }
      if (verbose > 0) {
         printf("%s 0x%08lx size 0x%08lx (%10s) distance 0x%04lx (%3s), flags 0x%02x prot "
                "0x%02x km_flags 0x%02x fn %p\n",
                type,
                reg->start,
                reg->size,
                out_sz(reg->size),
                reg->start - old_end,
                out_sz(reg->start - old_end),
                reg->flags,
                reg->protection,
                reg->km_flags.data32,
                reg->filename);
      }
      old_end = reg->start + reg->size;
   }
   return 0;
}

sigjmp_buf jbuf;
int fail;
int mmap_test(mmap_test_t* tests);
void sig_handler(int signal);
