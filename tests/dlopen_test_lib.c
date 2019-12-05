
/*
 * build: ./tools/kontain-gcc -shared -fPIC hello_lib.c -o hello_lib.so
 */
#include <stdio.h>

int do_function()
{
   printf("hello from do_function\n");
   return 1;
}
