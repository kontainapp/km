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
 *
 * Test for dlopen.
 */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
   printf("hello world\n");

   void* dl = dlopen("./dlopen_test_lib.so", RTLD_NOW);
   if (dl == NULL) {
      fprintf(stderr, "dlopen failed: %s\n", dlerror());
      return 1;
   }
   void (*fn)() = dlsym(dl, "do_function");
   if (fn == NULL) {
      fprintf(stderr, "dlsym failed: %s\n", dlerror());
      return 1;
   }
   fn();
   dlclose(dl);
   return 0;
}
