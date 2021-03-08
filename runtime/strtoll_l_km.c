#include <stdlib.h>

unsigned long long strtoll_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoull(nptr, endptr, base);
}

double __strtof_internal(const char* nptr, char** endptr, int group, void* unused)
{
   return strtof(nptr, endptr);
}

double __strtod_internal(const char* nptr, char** endptr, int group, void* unused)
{
   return strtod(nptr, endptr);
}