
#include <stdio.h>
#include <sys/utsname.h>

int main(int argc, char* argv[])
{
   struct utsname name;

   printf("Hello World from the following runtime environment: \n");
   if (uname(&name) >= 0) {
      printf("sysname \t= %s\nnodename \t= %s\nrelease \t= %s\nversion \t= %s\nmachine \t= %s\n",
             name.sysname,
             name.nodename,
             name.release,
             name.version,
             name.machine);
   }
   return 0;
}
