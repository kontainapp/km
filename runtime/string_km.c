#include "musl/src/string/strdup.c"
#include "musl/src/string/strndup.c"

#define strdup_attr __attribute__((nothrow)) __attribute__((leaf)) __attribute__((malloc))

weak_alias(strdup, __strdup) strdup_attr;
weak_alias(strndup, __strndup) strdup_attr;
