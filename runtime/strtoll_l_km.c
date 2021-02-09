#include <stdlib.h>

unsigned long long strtoll_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoull(nptr, endptr, base);
}
