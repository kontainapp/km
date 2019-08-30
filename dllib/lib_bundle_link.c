/* Auto-generated - Do not edit */
#include <stddef.h>
#include "km_dl_linkage.h"

/* dllib1 */
extern void * f_dllib1;
extern void * init_dllib1;
static km_dlsymbol_t _dllib1_syms[2] = {
  {.sym_name = "f", .sym_addr = &f_dllib1 },
  {.sym_name = "init", .sym_addr = &init_dllib1 },
};

/* dllib2 */
extern void * xyzzy_dllib2;
extern void * f_dllib2;
extern void * init_dllib2;
static km_dlsymbol_t _dllib2_syms[3] = {
  {.sym_name = "xyzzy", .sym_addr = &xyzzy_dllib2 },
  {.sym_name = "f", .sym_addr = &f_dllib2 },
  {.sym_name = "init", .sym_addr = &init_dllib2 },
};

km_dlentry_t __km_dllist[2] = {
  {.dlname="dllib1.so", .symbols=_dllib1_syms, .nsymbols=2 },
  {.dlname="dllib2.so", .symbols=_dllib2_syms, .nsymbols=3 },
};
int __km_ndllist = 2;
