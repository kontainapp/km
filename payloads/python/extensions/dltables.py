#!/bin/env python3
#
# prep dlopen/dlsym tables for a module from bear.out
#

import os
import subprocess
import sys
import sysconfig
import hashlib
import jinja2  # TODO - make sure it's installed (run pip3 install --user)

# setup.py puts binary builds here. PYTHON SPECIFIC
BUILD_LIB_PREFIX = "build/lib.{}-{}/".format(sysconfig.get_platform(),
                                             sysconfig.get_python_version())

# jinja2 template for generated .c file.
symfile_template = """
/*
 * GENERATED FILE. DO NOT EDIT.
 *
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
 * Registration and dynsym tables for {{ so }}
 */

# include "dlinfo_km.h"

extern void * {{ symbols[0].munged }} {% for sym in symbols[1:] %}, *{{ sym.munged }} {% endfor %};

static km_dl_symbols_t _km_symbols[] = { {% for sym in symbols %}
   { .name = "{{ sym.name }}", .sym = .&{{ sym.munged }} }, {% endfor %}

   { .name = NULL, .sym = NULL }
};

static km_dl_info_t km_dl_info = {
   .name = "{{ mod }}", .symtable = _km_symbols
};

__attribute__((constructor))  static void _km_dlinit(void) {
   km_dl_register(&km_dl_info);
}
"""


def python_mod_convert(so_name, so_suffix):
    """
    Gets .so name and converts it to unique _km_shortname_md5digest.
    Shortname is python module's name
    E.g. sompath/umath_linalg.cpython-37m-x86_64-linux-gnu.so with tag=numpy -> _km_umath_linalg_a4dea1cb40b5b1d2204fa65500d11800
    Returns unique modified name
    """

    mod = os.path.basename(so_name)[: -len(so_suffix)]
    #  with open(so_name, 'r+b') as f:
    #      content = f.read()
    #  return "_km_{short}_{digest}".format(digest=hashlib.md5(content).hexdigest(),
    #                                       short=mod)
    return "_km_" + mod


def mod_sym(sym, mod):
    # TODO: clean up pkg/mod/munged name creation with convert function, it looks hacky
    """
    munges the symbol sym defined for module mod
    """
    return sym + mod


def process_file(file_name, so_suffix=".cpython-37m-x86_64-linux-gnu.so", convert=python_mod_convert):
    """
    analyze a file with build (cc) log, finds commands used to build.so, and
    generates *_dltab.c files with symbol tables for dlopen() faking.

    so_suffix  is an output of `python3-config - -extension-suffix`
    """

    location = os.path.dirname(file_name)

    with open(file_name, 'r') as f:
        lines_with_so = [l for l in f.readlines() if l.find(so_suffix) != -1]

    for line in lines_with_so:
        elements = line.split()
        so_name = [w for w in elements if w.endswith(so_suffix)][0]
        objs = [w for w in elements if w.endswith(".o")]
        print(so_name)
        print(objs)

        nm = subprocess.run(["nm", "-s", "--defined-only", "-Dg", os.path.join(location, so_name)],
                            capture_output=True, encoding="utf-8")
        # nm.stdout is a list of nm output lines, each has "address TYPE symname". Extract symnames array:
        names = [i.split()[2] for i in nm.stdout.splitlines()]

        # python-specific module
        mod = convert(so_name, so_suffix)
        with open(os.path.join(location, so_name.replace(so_suffix, ".sym.txt")), 'w') as f:
            f.writelines(["{} {}\n".format(i, mod_sym(i, mod)) for i in names])

        with open(os.path.join(location, mod + "_dlsym.c"), 'w') as f:
            symbols = [{"name": n, "munged": mod_sym(n, mod)} for n in names]
            f.write(jinja2.Template(symfile_template).render(so=so_name[len(BUILD_LIB_PREFIX):],
                                                             mod=mod,
                                                             symbols=symbols))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: {} file_name\n   file contains output with .so build commands".format(sys.argv[0]))
    else:
        process_file(sys.argv[1])
