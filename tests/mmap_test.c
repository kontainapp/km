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
 * Basic test for mmap() and friends.
 */
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "km.h"
#include "km_mem.h"

int g_km_info_verbose;   // 0 is silent
static int err_count = 0;

static inline void usage()
{
   errx(1,
        "Usage: mmap [-V] \n"
        "Options:\n"
        "\t-V      - turn on Verbose printing of internal trace messages\n");
}

// human readable print for addresses and sizes
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

// positive tests
typedef struct mmap_test {
   char* test_info;        // string to help identify the test. NULL indicates the end of table
   int type;               // 1 for mmap, 0 for unmap
   uint64_t offset;        // for unmap, start offset from the last mmap result. For mmap: address
   size_t size;            // size for the operation
   int prot;               // protection for mmap()
   int flags;              // flags for mmap()
   int expected_failure;   // 0 if success is expected. Expected errno otherwise.
} mmap_test_t;

#define TYPE_MMAP 1
#define TYPE_MUNMAP 0

// Positive mmap/unmap tests. After this set , the free/busy lists in mmaps should be empty and tbrk
// should reset to top of the VA
static mmap_test_t tests[] = {
    {"Basic", TYPE_MMAP, 0, 8 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Basic", TYPE_MUNMAP, 0, 8 * MIB, 0, 0},
    {"Large", TYPE_MMAP, 0, 2 * GIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Large", TYPE_MUNMAP, 0, 2 * GIB, 0, 0},
    {"Multiple regions", TYPE_MMAP, 0, 2 * GIB + 12 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Multiple regions", TYPE_MUNMAP, 0, 2 * GIB + 12 * MIB, 0, 0},
    {"Swiss cheese", TYPE_MMAP, 0, 2 * GIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS},
    {"Swiss cheese", TYPE_MUNMAP, 512 * MIB, GIB, 0, 0},
    {"Swiss cheese", TYPE_MUNMAP, 0, 512 * MIB, 0, 0},
    {"Swiss cheese", TYPE_MUNMAP, 1 * GIB + 512 * MIB, 512 * MIB, 0, 0},

    {"Wrong args", TYPE_MMAP, 0x20000ul, 8 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, EINVAL},
    {"Wrong args", TYPE_MUNMAP, 0x20000ul, 1 * MIB, 0, 0, EINVAL},
    {NULL},
};

int main(int argc, char* const argv[])
{
   void* last_addr = 0;
   int mmap_failed = 0;
   int ret;
   int opt;
   extern char* __progname;
   __progname = "mmap_test";   // warn*() uses that

   while ((opt = getopt(argc, argv, "V")) != -1) {
      switch (opt) {
         case 'V':
            g_km_info_verbose = 1;
            break;
         case '?':
         default:
            usage();
      }
   }

   // positive tests
   for (mmap_test_t* t = tests; t->test_info != NULL; t++) {
      if (t->expected_failure != 0) {
         continue;
      }
      switch (t->type) {
         case TYPE_MMAP:
            if ((last_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0)) == MAP_FAILED) {
               warn("FAILED: mmap %s sz=%s", t->test_info, out_sz(t->size));

               mmap_failed = 1;
               err_count++;
               break;
            }
            memset(last_addr + t->size / 2, '2', t->size / 4);
            warnx("'%s' mmap passed", t->test_info);
            break;
         case TYPE_MUNMAP:
            if (mmap_failed) {
               warnx("\tprior mmap failed, skipping munmap test %s", t->test_info);
               break;
            }
            if ((ret = munmap(last_addr + t->offset, t->size)) != 0) {
               warn("FAILED: '%s' munmap(%p, %s) failed with rc %d",
                    t->test_info,
                    last_addr + t->offset,
                    out_sz(t->size),
                    ret);
               err_count++;
               break;
            }
            warnx("'%s' munmap passed", t->test_info);
            break;
         default:
            assert("No way" == NULL);
      }
   }

   // negative tests
   for (mmap_test_t* t = tests; t->test_info != NULL; t++) {
      if (t->expected_failure == 0) {
         continue;
      }
      switch (t->type) {
         case TYPE_MMAP:
            if ((last_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0)) != MAP_FAILED) {
               warn("FAILED: mmap %s sz=%s", t->test_info, out_sz(t->size));
               err_count++;
               break;
            }
            warn("Expected failure: '%s' mmap passed, errno %d", t->test_info, errno);
            break;
         case TYPE_MUNMAP:
            if ((ret = munmap(last_addr + t->offset, t->size)) == 0) {
               warn("FAILED: '%s' munmap(%p, %s) returned %d, expected %d",
                    t->test_info,
                    last_addr + t->offset,
                    out_sz(t->size),
                    ret,
                    t->expected_failure);
               err_count++;
               break;
            }
            warn("Expected failure: '%s' munmap passed, errno %d", t->test_info, errno);
            break;
         default:
            assert("Should never get here" == NULL);
      }
   }
   warnx("Resulting err_count=%d", err_count);
   exit(err_count);
}
