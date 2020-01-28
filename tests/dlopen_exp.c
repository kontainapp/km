#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define CRYPT_SYMBOL "crypt"
#define CRYPT_LIB "/usr/lib64/libcrypt.so"

/*
 * gcc -g -o dlopen_exp dlopen_exp.c -lm -ldl
 */

static void __attribute__((noinline)) hit_breakpoint(void* symvalue)
{
   printf("time returns %ld, symvalue %p\n", time(NULL), symvalue);
}

int main(int argc, char* argv[])
{
   float result;
   void* n;
   struct link_map* lmp;
   struct link_map* lmnext;
   int rc;
   void* symvalue = NULL;

   result = cos(M_PI * 2 / 8);   // 45 degrees
   printf("cosine of 45 degrees %f\n", result);

   // Dynamically load libcrypt and call crypt().
   void* c = dlopen(CRYPT_LIB, RTLD_LAZY);
   if (c == NULL) {
      printf("Couldn't dlopen() %s\n", CRYPT_LIB);
   } else {
      dlerror();
      symvalue = dlsym(c, CRYPT_SYMBOL);
      printf("symbol %s has value %p\n", CRYPT_SYMBOL, symvalue);
      char* dlsym_error = dlerror();
      if (dlsym_error != NULL) {
         printf("Couldn't find the value of symbol %s, error %s\n", CRYPT_SYMBOL, dlsym_error);
      } else {
         char* (*cryptfuncp)(char*, char*);
         cryptfuncp = symvalue;
         char* phrase = (*cryptfuncp)("mary had a little lamb", "salt");
         printf("%s returned phrase %s\n", CRYPT_SYMBOL, phrase);
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
