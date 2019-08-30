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
 */

#include <stdio.h>
#include <dlfcn.h>

#include "dllib.h"

// extern void* __km_dllist;

void print_msg(const char* msg)
{
   fprintf(stderr, "msg:%s\n", msg);
}

int main(int argc, char* argv[])
{
   void *dlhand1 = dlopen("dllib1.so", 0);
   if (dlhand1 == NULL) {
      fprintf(stderr, "dlopen(1) failed\n");
      return 1;
   }
   void *dlhand2 = dlopen("dllib2.so", 0);
   if (dlhand2 == NULL) {
      fprintf(stderr, "dlopen(2) failed\n");
      return 1;
   }
   void (*m1_init)(void) = dlsym(dlhand1, "init");
   void (*m2_init)(void) = dlsym(dlhand2, "init");
   m1_init();
   m2_init();

   dlclose(dlhand2);
   dlclose(dlhand1);
   return 0;
}
