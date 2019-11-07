#!/bin/env python3
#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Prepare dlopen/dlsym tables for a module from a build log output
#

import os
import subprocess
import sys
import sysconfig
import hashlib
import json
import jinja2  # TODO - make sure it's installed (run pip3 install --user)

# setup.py puts binary builds here. PYTHON SPECIFIC
BUILD_LIB_PREFIX = "build/lib.{}-{}/".format(sysconfig.get_platform(),
                                             sysconfig.get_python_version())

symtab_suffix = ".km.symmap"
symbols_c_suffix = ".km.symbols.c"

# jinja2 template for generated .c file.
#  FYI, jinja2 uses
#    {% ... %} for Statements
#    {{ ... }} for Expressions to print to the template output
#    {# ... #} for Comments not included in the template output
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

# include <stdlib.h>
# include "dlstatic_km.h"

{# first element does not have ',' in front, so we have to separate [0] and the list tail #}
extern void * {{ symbols[0].munged }} {% for sym in symbols[1:] %}, *{{ sym.munged }} {% endfor %};

static km_dl_symbol_t _km_symbols[] = { {% for sym in symbols %}
   { .name = "{{ sym.name }}", .addr = &{{ sym.munged }} }, {% endfor %}

   { .name = NULL, .addr = NULL }
};

static km_dl_lib_reg_t _km_dl_lib = {
   .name = "{{ mod }}", .symtable = _km_symbols
};

__attribute__((constructor))  static void _km_dlinit(void) {
   km_dl_register(&_km_dl_lib);
}
"""

# Jinja2 template for generated Makefile (.mk)
makefile_template = """#
# GENERATED FILE. DO NOT EDIT.
#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# The makefile does the following:
#   uses objcopy to preprocess .o files (replace symbols) based on base.km.symmap
#   compiles (-c) base.km.symbols.c file
#   builds .a for each .so
#   links the result by passing .o files explicitly and .a files as -l

# TODO - convert Obj and get link status

# TODO - pass it !
KM_RUNTIME_INCLUDES := /home/msterin/workspace/km/runtime
CFLAGS := -g -I$(KM_RUNTIME_INCLUDES)
KM_LIB_EXT := .km.lib.a

LIBS := {% for lib in libs %}\\\n\t{{ lib | replace(".so", "${KM_LIB_EXT}") }} {% endfor %}
SYMOBJ := $(subst ${KM_LIB_EXT},.km.symbols.o,$(LIBS))

all: $(SYMOBJ) $(LIBS)
\t@tmp=`mktemp`; for i in ${SYMOBJ} ${LIBS} ; do echo $$(realpath $$i) >> $$tmp ; done && echo Saved link line to $$tmp
\t@echo TODO: make this line shorter pack all .o into one .a and force it with --all-archive and rename .a to libxx.a and use -L -l

{# print ".a: obj_list" dependencies #}
{% for line in info %}
{{ line["so"] | replace(".so", "${KM_LIB_EXT}") }} : {{ line["objs"] | join(' \\\\\\n\\t\\t') }}
\tfor i in $< ; do objcopy --redefine-syms={{ line["so"] | replace(".so", ".km.symmap") }} $$i; done
\t@ar r $@ $<
{% endfor %}

DEBUG_FULL_INFO = {{ info }}
"""


def convert(so_name, so_suffix):
    """
    Calculates md5 digest of so_name file and returns a unique id AND file name base_<id>.
    """

    if not so_name.endswith(so_suffix):
        raise Exception("Wrong suffix {} for file{}".format(so_suffix, so_name))

    base = os.path.basename(so_name)[: -len(so_suffix)]
    with open(so_name, 'r+b') as f:
        id = "_" + hashlib.md5(f.read()).hexdigest() + "_km"
    return id, base + id


def process_file(file_name, so_suffix):
    """
    analyze a file with build (cc) log, finds commands used to build.so, and
    generates *_dltab.c files with symbol tables for dlopen() faking.

    so_suffix is an suffix of so files (e.g. output of `python3-config - -extension-suffix` for python)
    """

    location = os.path.realpath(os.path.dirname(file_name))
    with open(file_name, 'r') as f:
        lines_with_so = [l for l in f.readlines() if l.find(so_suffix) != -1]

    if len(lines_with_so) == 0:
        print("{} suffix is not mentioned in {} - nothing to do".format(so_suffix,
                                                                        file_name))
        return

    mk_info = []  # accumulated data for makefile generation
    for line in lines_with_so:
        elements = line.split()
        so_file_name = [w for w in elements if w.endswith(so_suffix)][0]
        objs = [w for w in elements if w.endswith(".o")]
        so_full_path_name = os.path.join(location, so_file_name)
        id, short_name = convert(so_full_path_name, so_suffix)

        nm = subprocess.run(["nm", "-s", "--defined-only", "-Dg",
                             os.path.join(location, so_file_name)],
                            capture_output=True, encoding="utf-8")
        if nm.returncode != 0:
            print("** NM FAILED for {}. stderr={}".format(so_file_name, nm.stderr))
            continue

        # nm.stdout is a list of nm output lines, each has "address TYPE symname". Extract symnames array:
        names = [i.split()[2] for i in nm.stdout.splitlines()]
        symbols = [{"name": n, "munged": n + id} for n in names]
        meta_data = {"so": so_file_name, "id": id, "short_name": short_name, "objs": objs}
        mk_info.append(meta_data)
        try:
            # Save file with symbols munging , for use with objcopy
            with open(so_full_path_name.replace(so_suffix, symtab_suffix), 'w') as f:
                f.writelines(["{} {}\n".format(s['name'], s['munged']) for s in symbols])

            # Write C file with init symtables
            c_table_name = so_full_path_name.replace(so_suffix, symbols_c_suffix)
            with open(c_table_name, 'w') as f:
                f.write(jinja2.Template(symfile_template).render(so=so_file_name[len(BUILD_LIB_PREFIX):],
                                                                 mod=short_name,
                                                                 symbols=symbols))
             # TODO run clang-format here
            try:
                cl = subprocess.run(["clang-format", "-i", "-style=file",
                                     c_table_name], capture_output=False, encoding="utf-8")
                if cl.returncode != 0:
                    print("FORMAT FAILED for {}".format(c_table_name))
            except FileNotFoundError as e:
                print("clang-format not found. Skipping format of {}".format(c_table_name))

            # write json file with meta data, FFU
            with open(so_full_path_name.replace(so_suffix, ".km.json"), 'w') as f:
                f.write(json.dumps(meta_data, indent=3))
        except Exception as e:
            print("*** FAILED when handling {}. More info:{}".format(so_file_name, meta_data))
            print(e)
            continue

    # and finally, makefile generation
    mk_name = os.path.join(location, "dlstatic_km.mk")
    with open(mk_name, 'w') as f:
        f.write(jinja2.Template(makefile_template).render(libs=[i["so"] for i in mk_info],
                                                          info=mk_info, symtab_suffix=symtab_suffix))

    # Debug - info for Makefile creation
    mk_json_name = os.path.join(location, "dlstatic_km.mk.json")
    with open(mk_json_name, 'w') as f:
        f.write(json.dumps(mk_info, indent=3))

    print("Generated {} and related files".format(mk_name))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: {} file_name\n   file contains output with .so build commands".format(sys.argv[0]))
    else:
        process_file(sys.argv[1], so_suffix=".so")
