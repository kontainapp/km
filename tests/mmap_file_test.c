/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Basic test for mmap() and friends on files
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mmap_test.h"

const char* fname = "/opt/kontain/runtime/libruntime.a";

TEST mmap_file_test(void)
{
   int fd = open(fname, O_RDONLY);

   void* base = mmap(0, 0x10000, PROT_READ, MAP_PRIVATE, fd, 0);
   ASSERT_NEQ(MAP_FAILED, base);

   off_t off = 0x1000;
   void* addr = mmap(base + off, 0x1000, PROT_READ | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, fd, off);
   ASSERT_EQ(base + off, addr);

   off = 0x3000;
   addr = mmap(base + off, 0x1000, PROT_READ, MAP_FIXED | MAP_PRIVATE, fd, off);
   ASSERT_EQ(base + off, addr);

   off = 0x5000;
   addr = mmap(base + off, 0x1000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE, fd, off);
   ASSERT_EQ(base + off, addr);

   off = 0x7000;
   addr = mmap(base + off,
               0x1000,
               PROT_READ | PROT_WRITE,
               MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_ANONYMOUS,
               -1,
               off);
   ASSERT_EQ(base + off, addr);

   close(fd);

   close(fd);
   printf("before unmap> ");
   fflush(stdout);
   getchar();

   ASSERT_NEQ(-1, munmap(base, 0x10000));

   printf("after unmap> ");
   fflush(stdout);
   getchar();
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   RUN_TEST(mmap_file_test);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
