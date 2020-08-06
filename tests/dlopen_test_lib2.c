
/*
 * build: ./tools/bin/kontain-gcc -shared -fPIC hello_lib.c -o hello_lib.so
 */
#include <stdio.h>

int do_function()
{
   printf("hello from lib2:do_function\n");
   return 2;
}
