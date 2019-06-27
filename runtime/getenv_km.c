#include "musl/src/env/getenv.c"

char* secure_getenv(const char* name) __attribute__((alias("getenv")));
