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
 * A helper program to test gdb's ability to read and alter memory pages that
 * deny all access to those pages.
 * All we do is allocated a piece of memory with no permitted access.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc, char* argv[])
{
   void* memchunk;
   const int chunksize = 2 * 4096;
   unsigned char* imemchunk __attribute__((unused));

   memchunk = mmap(NULL, chunksize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (memchunk == MAP_FAILED) {
      printf("mmap() failed, %d, %s\n", errno, strerror(errno));
   }
   imemchunk = memchunk;

   printf("memchunk %p\n", memchunk);

   return 0;
}
