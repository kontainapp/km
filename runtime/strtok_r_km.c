#define strtok_r __strtok_r
#include "musl/src/string/strtok_r.c"
#undef strtok_r
weak_alias(__strtok_r, strtok_r);
