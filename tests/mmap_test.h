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

static int in_gdb = 1;

extern int main(int argc, char** argv);
#define KM_PAYLOAD() ((uint64_t)&main < 4 * MIB)   // in KM, we load from 2Mb, In Linux, from 4MB
#define ASSERT_MMAPS_COUNT(expected_count, query)                                                  \
   if (KM_PAYLOAD() == 1 && in_gdb == 1) {                                                         \
      /*get_maps(greatest_get_verbosity());*/                                                      \
      char read_check_result[256];                                                                 \
      int verbosity = greatest_get_verbosity() >= 1;                                               \
      sprintf(read_check_result, "%i,%i,%i", query, verbosity, expected_count);                    \
      int ret = read(-2020, read_check_result, sizeof(read_check_result));                         \
      ASSERT_EQ_FMT(-1, ret, "%d");                                                                \
      if (errno == EBADF) {                                                                        \
         fprintf(stderr, "\nPlease run this test in gdb\n");                                       \
         in_gdb = 0;                                                                               \
      } else {                                                                                     \
         ASSERT_EQ_FMT(ESPIPE, errno, "%d");                                                       \
         ASSERT_NOT_EQm(NULL, strstr("True", read_check_result), "Got False");                     \
      }                                                                                            \
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
static const char* errno_fmt UNUSED = "errno 0x%x";   // format for offsets/types error msg
static const char* ret_fmt UNUSED = "ret 0x%x";       // format for offsets/types error msg
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

sigjmp_buf jbuf;
int fail;
int mmap_test(mmap_test_t* tests);
void sig_handler(int signal);
