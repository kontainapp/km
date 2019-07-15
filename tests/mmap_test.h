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
 * Useful headers for mmap tests
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "km_mem.h"

typedef enum { TYPE_MMAP, TYPE_MUNMAP, TYPE_MPROTECT } call_type_t;
typedef struct mmap_test {
   char* test_info;        // string to help identify the test. NULL indicates the end of table
   call_type_t type;       // 1 for mmap, 0 for unmap
   uint64_t offset;        // for unmap & mprotect, start offset from the last mmap result.
                           // for mmap: address (if applicable)
   size_t size;            // size for the operation
   int prot;               // protection for mmap()
   int flags;              // flags for mmap()
   int expected_failure;   // 0 if success is expected. Expected errno otherwise.
} mmap_test_t;

#define OK 0   // expected good result