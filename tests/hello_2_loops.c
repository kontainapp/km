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

static const char* msg1 = "brick in the wall ";
static const char* msg2 = "one bites the dust";
const long step = 1ul;

void subrun(char* msg)
{
   for (long run_count = 0; run_count < step; run_count++) {
      if (run_count % step == 0) {
         printf("Another %s # %ld (%ld)\n", msg, run_count / step, run_count);
      }
   }
   pthread_exit(0);
}

void run(char* msg)
{
   for (long run_count = 0; run_count < 32; run_count++) {
      pthread_t pt1, pt2;

      pthread_create(&pt1, NULL, (void* (*)(void*))subrun, (void*)msg1);
      pthread_create(&pt2, NULL, (void* (*)(void*))subrun, (void*)msg2);
      printf(" ... joined 0x%lx, %d\n", pt2, pthread_join(pt2, NULL));
      printf(" ... joined 0x%lx, %d\n", pt1, pthread_join(pt1, NULL));
   }
}

int main()
{
   pthread_t pt1, pt2;

   pthread_create(&pt1, NULL, (void* (*)(void*))run, NULL);
   printf("started 0x%lx\n", pt1);
   pthread_create(&pt2, NULL, (void* (*)(void*))run, NULL);
   printf("started 0x%lx\n", pt2);

   printf("joining 0x%lx ... \n", pt1);
   printf("joined 0x%lx, %d\n", pt1, pthread_join(pt1, NULL));
   printf("joining 0x%lx ... \n", pt2);
   printf("joined 0x%lx, %d\n", pt2, pthread_join(pt2, NULL));
   exit(0);
}
