#include <unistd.h>

#include "km_hcalls.h"

static const char *msg = "Hello, world\n";

static inline void km_hcall(int n, volatile void *arg)
{
   __asm__ __volatile__("outl %0, %1"
                        :
                        : "a"((uint32_t)((uint64_t)arg)),
                          "d"((uint16_t)(KM_HCALL_PORT_BASE + n))
                        : "memory");
}

ssize_t write(int fildes, const void *buf, size_t nbyte)
{
   km_stdout_hc_t arg = {.data = (uint64_t)buf, .length = nbyte};

   km_hcall(KM_HC_STDOUT, &arg);
   return nbyte;
}

void exit(int status)
{
   km_hlt_hc_t arg = {.exit_code = status};

   km_hcall(KM_HC_HLT, &arg);
}

int main()
{
   write(1, msg, 14);
   exit(17);
}