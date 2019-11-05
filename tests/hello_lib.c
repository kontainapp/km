
/*
 * build: ./tools/kontain-gcc -shared -fPIC hello_lib.c -o hello_lib.so
 */
#include <stdio.h>

void do_function()
{
	printf("hello from do_function\n");
}
