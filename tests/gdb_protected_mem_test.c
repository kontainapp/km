/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * A helper program to test gdb's ability to read and alter memory pages that
 * deny all access to those pages.
 * All we do is allocated a piece of memory with no permitted access.
 */
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

int main(int argc, char* argv[])
{
   void* memchunk;
   const int chunksize = 2*4096;
   unsigned char* imemchunk __attribute__ ((unused));

   memchunk = mmap(NULL, chunksize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (memchunk == MAP_FAILED) {
      printf("mmap() failed, %d, %s\n", errno, strerror(errno));
   }
   imemchunk = memchunk;

   printf("memchunk %p\n", memchunk);

   return 0;
}
