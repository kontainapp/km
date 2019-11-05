#include <stdlib.h>

typedef struct {
   const char* const name;
   void* const sym;
} km_dl_symbols_t;

typedef struct {
   const char* const name;
   const km_dl_symbols_t* const symtable;
} km_dl_info_t;

extern void* km_dl_register(km_dl_info_t*);
