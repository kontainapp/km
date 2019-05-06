#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static jmp_buf buf;

static void jump(void)
{
   longjmp(buf, 17);
}

void func(void)
{
   jump();
}

static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";
   int pass;

   pass = setjmp(buf);
   printf("=== %d === %s %s\n", pass, msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   if (pass) {
      exit(0);
   }
   func();
}
