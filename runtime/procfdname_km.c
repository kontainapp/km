#include "syscall.h"

void __procfdname(char *buf, unsigned fd)
{
   __syscall2(HC_procfdname, buf, fd);
}