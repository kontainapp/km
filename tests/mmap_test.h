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
 * Useful headers for mmap tests
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "km_mem.h"

extern int main(int argc, char** argv);
#define ASSERT_MMAP_FD -2020   // This should be the same as used in gdb_simple_test.py
#define KM_PAYLOAD() ((uint64_t)&main < 4 * MIB)   // in KM, we load from 2Mb, In Linux, from 4MB
#define ASSERT_MMAPS_COUNT(_expected_count, _query)                                                \
   {                                                                                               \
      int ret = maps_count(_expected_count, _query);                                               \
      ASSERT_NOT_EQm("Expected mmaps counts does not match ", -1, ret);                            \
   }

/*
 * The expected number of busy memory regions for a freshly started payload.
 * If you add regions to km's busy memory list, you will need to change
 * this value.
 */
#define INITIAL_BUSY_MEMORY_REGIONS 6

// Get the initial busy count for use in later calls to ASSERT_MMAPS_CHANGE()
#define ASSERT_MMAPS_INIT(initial_busy)                                                            \
   {                                                                                               \
      initial_busy = INITIAL_BUSY_MEMORY_REGIONS;                                                  \
      int ret = maps_count(initial_busy, BUSY_MMAPS);                                              \
      ASSERT_NOT_EQ(ret, -1);                                                                      \
   }

// Check to see if busy memory region count is as expected.
#define ASSERT_MMAPS_CHANGE(expected_change, initial_busy)                                         \
   {                                                                                               \
      int expected_count = expected_change + initial_busy;                                         \
      int ret = maps_count(expected_count, TOTAL_MMAPS);                                           \
      ASSERT_NOT_EQ(ret, -1);                                                                      \
   }

// Type of operation invoked by a single line in test tables
typedef enum {
   TYPE_MMAP,   // see mmap_test_t below
   TYPE_MMAP_AUX,
   TYPE_MUNMAP,
   TYPE_MPROTECT,
   TYPE_MREMAP,   // for remapped memory, PROT_WRITE should have been set on mmmap (test will write!)
   TYPE_READ,     // do a read to <offset, offset+size> from last mmap or mremap
   TYPE_WRITE,    // do a write to <offset, offset+size> from last mmap (use 'prot' for data)
   TYPE_USE_MREMAP_ADDR,   // in further tests, base all on address returned by last mremap (not mmap)
   TYPE_MADVISE,

} call_type_t;

typedef enum { BUSY_MMAPS = 1, FREE_MMAPS = 2, TOTAL_MMAPS = 3 } write_querry_t;

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
static const char* const errno_fmt UNUSED = "errno 0x%x";   // format for offsets/types error msg
static const char* const ret_fmt UNUSED = "ret 0x%x";       // format for offsets/types error msg
// just to type less going forward
static const int flags = (MAP_PRIVATE | MAP_ANONYMOUS);

static const int MAX_MAPS = 4096;

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

extern sigjmp_buf jbuf;
extern int fail;
enum greatest_test_res mmap_test(mmap_test_t* tests);
void sig_handler(int signal);
int maps_count(int expected_count, int query);