/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Prototype for dlopen(2) based plugins in languages runtimes like python
 * and nodejs where native language extensions are packaged in shared libraries.
 * 
 * For each native language extension library that Kontain chooses to
 * support, Kontain builds that library into a regular relocatable .o file
 * where all symbols exported from the .o have been transformed to avoid 
 * collision with other transformed extentsion libraries.
 * 
 * In the example here, each extension module exposes a 'init' function
 * and an integer 'data'. There are two extention modules, 'module1' and 'module2'.
 * The main module exposes a 'print_msg' function to the extension modules.
 * 
 * The Kontain packaging tools have transformed the extension module symbol names
 * and created linkage tables that are included in the payload. When linkage table
 * is present, KM uses it to implement dlopen(2) and dlsym(2).
 */

#include <dlfcn.h>
#include <stdio.h>
#include "km_dl_linkage.h"

/*
 * This would be in a header file included by the all extension
 * modules. Think 'Python.h'.
 */
void print_msg(char *);

/*
 * Module 1 would have exported a function called init().
 * KM build process transforms the name to module1_init().
 */
int module1_data = 1;
void module1_init()
{
    print_msg("from module 1");
}

/*
 * Module 2 would have exported a function called init().
 * KM build process transforms the name to module2_init().
 */
int module2_data = 2;
void module2_init()
{
    print_msg("from module 2");
}

/*
 * The modlinkage table is created by Kontain packaging tools.
 * this allows KM to map virtual .so files and preloaded library.
 */
km_dlsymbol_t  _km_module1_symbols[] = {
    {.sym_name = "init", .sym_addr = &module1_init},
    {.sym_name = "data", .sym_addr = &module1_data},
};
km_dlsymbol_t  _km_module2_symbols[] = {
    {.sym_name = "init", .sym_addr = &module2_init},
    {.sym_name = "data", .sym_addr = &module2_data},
};
km_dlentry_t __km_dllist[] = {
    {.dlname="module1.so", .symbols = _km_module1_symbols, .nsymbols=2,},
    {.dlname="module2.so", .symbols = _km_module2_symbols, .nsymbols=2,},
};
int __km_ndllist = 2;

/*
 * This is the main program. Think CPython
 */

void print_msg(char *msg)
{
    printf("Got message: %s\n", msg);
}

int main(int argc, char *argv[])
{
    /*
     * Need this line to ensure that __km_dllist and __km_ndllist make it into the payload.
     * The pipeline that builds the customized language payload will take care of making sure
     * the linkage tables make it in.
     */
    printf("hello from dltest. __km_dllist=%p __km_ndllist=%d\n", __km_dllist, __km_ndllist);

    /*
     * Call module1's init function and retrive it's data.
     */
    void *module1 = dlopen("module1.so", RTLD_NOW);
    void (*m1_init)(void) = dlsym(module1, "init");
    m1_init();
    int *m1_data = dlsym(module1, "data");
    printf("module1.data=%d\n", *m1_data);

    /*
     * Do the same for module2.
     */
    void *module2 = dlopen("module2.so", RTLD_NOW);
    void (*m2_init)(void) = dlsym(module2, "init");
    m2_init();
    int *m2_data = dlsym(module2, "data");
    printf("module2.data=%d\n", *m2_data);

    dlclose(module1);
    dlclose(module2);
    return 0;
}