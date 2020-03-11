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
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

/*
 * A simple test program to load a shared library at runtime and call a
 * function in the library.
 * This program is used together with cmd_for_dlopen_test.gdb.
 * That gdb script runs "info sharedlibrary" and uses the value of
 * a dynamically loaded symbol to verify the library is loaded and we
 * are getting symbols from the library.
 * We also have code to follow the list of link_map structures in the
 * running image.  This is more for curiousity sake.  It can be used to
 * verify that the share library is loaded.
 */

#define SHARED_LIB "dlopen_test_lib.so"
#define SHARED_LIB_SYMBOL "do_function"

/*
 * gcc -g -o gdb_sharelib2_test gdb_sharelib2_test.c -ldl -lz
 */

static void __attribute__((noinline)) hit_breakpoint(void* symvalue)
{
   printf("time returns %ld, symvalue %p\n", time(NULL), symvalue);
}

static void __attribute__((noinline))got_symbol_breakpoint(void)
{
   printf("do something to trick the compiler\n");
}

int main(int argc, char* argv[])
{
   void* n;
   struct link_map* lmp;
   struct link_map* lmnext;
   int rc;
   void* symvalue = NULL;

   // Dynamically load our test shared library
   void* c = dlopen(SHARED_LIB, RTLD_LAZY);
   if (c == NULL) {
      printf("Couldn't dlopen() %s, does your LD_LIBRARY_PATH env var contain the tests directory\n", SHARED_LIB);
   } else {
      dlerror();
      symvalue = dlsym(c, SHARED_LIB_SYMBOL);
      printf("symbol %s has value %p\n", SHARED_LIB_SYMBOL, symvalue);
      char* dlsym_error = dlerror();
      if (dlsym_error != NULL) {
         printf("Couldn't find the value of symbol %s, error %s\n", SHARED_LIB_SYMBOL, dlsym_error);
      } else {
         int (*do_function)(void);
         got_symbol_breakpoint();
         do_function = symvalue;
         int rv = (*do_function)();
         printf("%s returned %d\n", SHARED_LIB_SYMBOL, rv);
      }
      dlclose(c);
   }
   printf("\n");

   hit_breakpoint(symvalue);

   // Follow the link_map list and print out info.
   n = dlopen(NULL, RTLD_NOLOAD);
   if (n == NULL) {
      printf("dlopen(NULL) returned NULL?\n");
      return 1;
   }
   rc = dlinfo(n, RTLD_DI_LINKMAP, &lmp);
   if (rc != 0) {
      printf("dlinfo( RTLD_DI_LINKMAP ) failed, error %s\n", dlerror());
      return 2;
   }

   for (lmnext = lmp; lmnext != NULL; lmnext = lmnext->l_next) {
      printf("lmnext %p\n", lmnext);
      printf("l_addr %lx\n", lmnext->l_addr);
      printf("l_name %s\n", lmnext->l_name);
      printf("l_ld %p\n", lmnext->l_ld);
      printf("l_next %p\n", lmnext->l_next);
      printf("l_prev %p\n\n", lmnext->l_prev);
   }

   dlclose(n);

   return 0;
}
