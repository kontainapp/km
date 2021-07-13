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
 * Manual helper to experiment with mprotect /map glueing in Linux . Based on 'man mprotect'
 */

#define _GNU_SOURCE /* See feature_test_macros(7) */

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#define handle_error(msg)                                                                          \
   do {                                                                                            \
      perror(msg);                                                                                 \
      exit(EXIT_FAILURE);                                                                          \
   } while (0)

static char* buffer;

static void handler(int sig, siginfo_t* si, void* unused)
{
   /* Note: calling printf() from a signal handler is not safe
      (and should not be done in production programs), since
      printf() is not async-signal-safe; see signal-safety(7).
      Nevertheless, we use printf() here as a simple way of
      showing that the handler was called. */

   printf("Got SIGSEGV at address: 0x%lx\n", (long)si->si_addr);
   exit(EXIT_FAILURE);
}

static void print_maps(char* msg)
{
   static char* maps = "/proc/self/maps";
   size_t from, to, offset;
   char prot[16] = {0};
   int major, minor, inode;
   char path[4096];
   char buf[4096];

   printf("================= %s\n", msg);
   FILE* file = fopen(maps, "r");
   if (file == NULL) {
      printf("can't open %s", maps);
      return;
   }
   while (fgets(buf, sizeof(buf), file)) {
      sscanf(buf, "%lx-%lx %s %lx %x:%x %d %s", &from, &to, prot, &offset, &major, &minor, &inode, path);
      if (strncmp(path, "/dev/", 4) != 0) {
         continue;
      }
      printf("0x%lx - 0x%lx  size 0x%-6lx (%-6ld) %s path %s\n", from, to, to - from, to - from, prot, path);
   }

   fclose(file);
}

int main(int argc, char* argv[])
{
   char* p;
   int pagesize;
   struct sigaction sa;

   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   sa.sa_sigaction = handler;
   if (sigaction(SIGSEGV, &sa, NULL) == -1)
      handle_error("sigaction");

   pagesize = 1024 * 1024;   // MIB

   /* Allocate a buffer aligned on a page boundary;
      initial protection is PROT_READ | PROT_WRITE */
   print_maps("START");

   void* buffer = mmap((void*)(16ul * 1024 * 1024 * 1024 * 1024),
                       5 * pagesize,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_SHARED,
                       -1,
                       0);
   if (buffer == MAP_FAILED) {
      handle_error("mmap");
   }
   print_maps("after mmap");
   printf("Start of region:        0x%lx\n", (long)buffer);

   if (mprotect(buffer + pagesize * 2, pagesize, PROT_READ) == -1)
      handle_error("mprotect");
   print_maps("after 1st mprotect");

   if (mprotect(buffer + pagesize * 2, pagesize, PROT_READ | PROT_WRITE) == -1)
      handle_error("mprotect");
   print_maps("after 2nd mrotect");

   // void* new = mremap(buffer, pagesize * 2, pagesize * 4, MREMAP_MAYMOVE);
   // if (new == MAP_FAILED) {
   //    handle_error("mremap");
   // }
   // printf("mremap: old: 0x%lx new: 0x%lx\n", (long)buffer, (long)new);
   // print_maps("after remap");

   // printf("mmap2\n");
   // void* buffer1 = mmap(buffer + 5ul * pagesize,
   //                      8 * pagesize,
   //                      PROT_READ | PROT_WRITE,
   //                      MAP_ANONYMOUS | MAP_SHARED,
   //                      -1,
   //                      0);
   // if (buffer1 == MAP_FAILED) {
   //    handle_error("mmap");
   // }
   // printf("map2: old: 0x%lx new: 0x%lx\n", (long)buffer, (long)buffer1);
   // print_maps("after 2nd mmmap");

   // void* new = mremap(MIN(buffer, buffer1) + pagesize, pagesize * 6, pagesize * 8,
   // MREMAP_MAYMOVE); if (new == MAP_FAILED) {
   //    handle_error("mremap");
   // }
   // printf("mmremap: old: 0x%lx new: 0x%lx\n", (long)buffer, (long)new);

   print_maps("after remap");

   // for (p = buffer;;)
   //    *(p++) = 'a';

   // printf("Loop completed - should never happen\n");
   exit(EXIT_SUCCESS);
}
