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

#include "km_mem.h"

// Type of operation invoked by a single line in test tables
typedef enum {
   TYPE_MMAP,   // see mmap_test_t below
   TYPE_MUNMAP,
   TYPE_MPROTECT,
   TYPE_MREMAP,   // for remapped memory, PROT_WRITE should have been set on mmmap (test will write!)
   TYPE_READ,     // do a read to <offset, offset+size> from last mmap or mremap
   TYPE_WRITE,    // do a write to <offset, offset+size> from last mmap (use 'prot' for data)
   TYPE_USE_MREMAP_ADDR,   // in further tests, base all on address returned by last mremap (not mmap)
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
   int flags;          // flags to pass to mmap() or mremap()
   int expected;       // 0 if success is expected. Expected signal (for read/write) or errno
} mmap_test_t;

#define OK 0   // expected good result

static const char* errno_fmt = "errno 0x%x";   // format for offsets/types error msg
static const char* ret_fmt = "ret 0x%x";       // format for offsets/types error msg
// just to type less going forward
static const int flags = (MAP_PRIVATE | MAP_ANONYMOUS);