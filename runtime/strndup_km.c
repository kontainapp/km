#include "musl/src/string/strndup.c"

weak_alias(strndup, __strndup) __attribute__((nothrow)) __attribute__((leaf)) __attribute__((malloc));
