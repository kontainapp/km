#include "musl/src/string/strdup.c"

weak_alias(strdup, __strdup) __attribute__((nothrow)) __attribute__((leaf)) __attribute__((malloc));