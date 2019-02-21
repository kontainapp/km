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
 */

#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

static const char* msg1 = "Hello, I am a loop ";

void run(char* msg, long run_count)
{
   const long step = 1ul << 26;

   run_count *= step;
   printf("run (almost) forever, count=%ld\n", run_count);
   // run until someone changes the 'run'. If run is <=0 on start, run forever
   while (--run_count != 0) {
      if (run_count % step == 0) {
         printf("Another %s # %ld (%ld) 0x%lx\n", msg, run_count / step, run_count, pthread_self());
      }
   }
}

void* run_2(void* arg)
{
   run("one bites the dust", 32);
   return NULL;
}

int main()
{
   pthread_t pt;

   puts(msg1);
   pthread_create(&pt, NULL, run_2, NULL);
   printf("started 0x%lx\n", pt);
   run("brick in the wall", 16);
   printf("joining 0x%lx ... \n", pt);
   printf("joined 0x%lx, %d\n", pt, pthread_join(pt, NULL));
   exit(0);
}
