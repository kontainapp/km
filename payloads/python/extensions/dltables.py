#!/bin/env python3
#
# prep dlopen/dlsym tables for a module from bear.out
# set -x
import os
import subprocess
import sys
import sysconfig
import hashlib


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

        nm = subprocess.run(["nm", "-s", "--defined-only", "-Dg", os.path.join(location, so_name)],
                            capture_output=True)
        #  encoding = "utf-8")
        names = [i.split()[2] for i in nm.stdout.splitlines()]
        # python-specific module
        mod = convert(so_name, so_suffix)

        # each generated file starts with this
        dlsym_prologue = """
/*
 * GENERATED FILE. DO NOT EDIT. See dltables.py
 *
 * registration and dynsym tables for {so}
 */

# include "km_dlsym.h"

static km_payload_syms_t symbols[] = {{
"""
        # each generated file ends with this
        dlsym_epilogue = """
}};

km_payload_dltab_t {mod} = {{ "{mod}", symbols }} ;
"""

        with open(os.path.join(location, mod + "_dlsym.c"), 'x') as f:
            f.write(dlsym_prologue.format(so=so_name))
            for n in names:
                f.write("{{ \"{sym}\", &{sym} }},\n".format(
                    sym=n))
            f.write(dlsym_epilogue.format(mod=mod))

    # mod_name =$(basename $so | sed "s/$so_suffix//")
    # out =${mod_name}_table.c
    # nm - s - -defined-only - Dg $so | awk "$(m4 -DMODULE=$mod_name $exedir/awk_script)" > $out
    # done


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("""
Usage: {} file_name
   file is .so name
""".format(sys.argv[0]))
    else:
        process_file(sys.argv[1])
