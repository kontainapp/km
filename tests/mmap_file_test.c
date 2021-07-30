/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
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
