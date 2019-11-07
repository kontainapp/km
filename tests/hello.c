/*
 * build: ./tools/kontain-gcc -shared -fPIC hello.c -o hello.km
 */
#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char *argv[])
{
   printf("hello world\n");

   void *dl = dlopen("./hello_lib.so", RTLD_NOW);
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
