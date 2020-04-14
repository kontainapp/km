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

#include "greatest/greatest.h"

#define ASSERT_DL(_dl, _lib)                                                                       \
   if (_dl == NULL) {                                                                              \
      printf("%s. File=%s (%s:%d) \n", dlerror(), _lib, __FUNCTION__, __LINE__);                   \
      ASSERT_NOT_EQ(NULL, _dl);                                                                    \
   }

TEST dlopen_test()
{
   void* dl = dlopen("./dlopen_test_lib.so", RTLD_NOW);
   ASSERT_DL(dl, "./dlopen_test_lib.so");
   void* dl2 = dlopen("./dlopen_test_lib2.so", RTLD_NOW);
   ASSERT_DL(dl2, "./dlopen_test_lib2.so");
   int (*fn)() = dlsym(dl, "do_function");
   ASSERT_NOT_EQ(NULL, fn);
   int (*fn2)() = dlsym(dl2, "do_function");
   ASSERT_NOT_EQ(NULL, fn2);
   ASSERT_EQ(1, fn());
   ASSERT_EQ(2, fn2());
   dlclose(dl);
   dlclose(dl2);
   PASS();
}

TEST dlopen_self_test()
{
   void* dl = dlopen(NULL, RTLD_NOW);
   ASSERT_NOT_EQ(NULL, dl);
   ASSERT_NOT_EQ(NULL, "main");
   dlclose(dl);
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(dlopen_test);
   RUN_TEST(dlopen_self_test);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
