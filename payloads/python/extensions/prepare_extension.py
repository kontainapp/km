#!/usr/bin/python3
#
# Copyright © 2019-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# prepare extantion (.so) for linking in and using with "static" dlopen/dlsym:
#
# - Analyze build log and generate neccessary files (.c, .id, etc)
# - Generate makefile to build the actual artifacts
#

"See ../README.md for information"

import os
import subprocess
import sys
import sysconfig
import hashlib
import json
import jinja2
import re
import fnmatch
import logging
from functools import reduce

symmap_suffix = ".km.symmap"
symbols_c_suffix = ".km.symbols.c" # note: should be in sync with makefile template's KM_SYM_EXT
id_suffix = ".km.id"
so_suffix = ".so"
so_pattern = re.compile(r".*\W-o\W+.*\w+\.so")  # e.g.'some_stuff -o aaa/ddd/a.so'

need_to_mung = True  # by default we munge the symnames

# jinja2 template for generated .c file.
#  FYI, jinja2 uses
#    {% ... %} for Statements
#    {{ ... }} for Expressions to print to the template output
#    {# ... #} for Comments not included in the template output
symfile_template = """/*
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
// symbols to pick up from .a. They are munged to avoid name overlap between different libs
extern void * {{ symbols[0].name_munged }} {% for sym in symbols[1:] %}, *{{ sym.name_munged }} {% endfor %};

static km_dl_symbol_t _km_symbols[] = { {% for sym in symbols %}
   { .name = "{{ sym.name }}", .addr = &{{ sym.name_munged }} }, {% endfor %}
   { .name = NULL, .addr = NULL }
};

// Registration info
static km_dl_lib_reg_t _km_dl_lib = {
   .name = "{{ so }}", .id = "{{ so_id }}", .symtable = _km_symbols
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

# TODO: the line below relies on specific locations in the source tree, need to use (and populate) /opt/kontain/include
KM_RUNTIME_INCLUDES ?=  $(shell echo ${CURDIR} | sed 's-payloads/python/.*-runtime-')
CFLAGS = -g -I$(KM_RUNTIME_INCLUDES) -I$(shell echo ${CURDIR})
LINK_LINE_FILE := linkline_km.txt
KM_LIB_EXT := .km.lib.a
KM_SYM_EXT := .km.symbols.o

LIBS := {% for lib in libs %}\\\n\t{{ lib | replace(".so", "${KM_LIB_EXT}") }} {% endfor %}

SYMOBJ := $(subst ${KM_LIB_EXT},.km.symbols.o,$(LIBS))

# Build libraries and create text file with .o and .a for linker
all: $(SYMOBJ) $(LIBS)
\t@rm -f ${LINK_LINE_FILE}
\t@for i in ${SYMOBJ} ${LIBS} ; \\
   do \\
     realpath $$i | sed 's-.*/cpython-.-' >> ${LINK_LINE_FILE} ; \\
   done && echo -e "Libs built successfully. Pass ${GREEN}@${CURDIR}/${LINK_LINE_FILE}${NOCOLOR} to ld for linking in"
\t@if [[ ! -z "{{ ldflags }}" ]] ; then  echo {{ ldpaths }} {{ ldflags }} >>  ${CURDIR}/${LINK_LINE_FILE} ; fi

clean:
\t@items=$$(find . -name '*km.*[oa]') ; if [[ ! -z $$items ]] ; then rm -v $$items ; fi

clobber:
\t-git clean -xdf

{# generate ".a: obj_list" dependencies #} {% for line in info %}
{{ line["so"] | replace(".so", "${KM_LIB_EXT}") }} : {{ line["so"] | replace(".so", "${KM_SYM_EXT}") }} \\\n\t{{ line["objs"] | join(' \\\\\n\t') }}
\t\t@id={{ line["id"] }} ; echo Processing library=$@ id=$$id; rm -f $@; \\
\t\tfor obj in $^ ; do \\
\t\t\tmunged_obj="/tmp/$$(basename $${obj/.o/$$id.o})" ; cp $$obj $$munged_obj; \\
\t\t\t{{ echo_if_no_mung }} objcopy --redefine-syms={{ line["so"] | replace(".so", ".km.symmap") }} $$munged_obj; \\
\t\t\tar qv $@ $$munged_obj ; rm -f $$munged_obj ; \\
\t\tdone
{% endfor %}

# allows to do 'make print-varname'
print-%  : ; @echo $* = $($*)

# fancy prints
ifeq (${PIPELINE_WORKSPACE},)
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m
endif
"""


def convert(so_name):
    """
    Calculates md5 digest of so_name file and returns a unique id AND file name base_<id>.
    """
    if not so_name.endswith(so_suffix):
        raise Exception(f"Wrong suffix {so_suffix} for file {so_name}")
    with open(so_name, 'r+b') as f:
        id = "_" + hashlib.md5(f.read()).hexdigest() + "_km"
    base = os.path.basename(so_name)[: -len(so_suffix)]
    return id, base + id


# list of symbols we do not want to mangle for a given set of .so
BLACKLIST = None


def sym_blacklist(location):
    """
    Forms blacklist for symnames mangling by checking all '.a' libs in the passed location
    adding their symbols to global BLACKLIST of symnames which we do not want to mangle
    It prevents .so libs built with '-l<lib>' flags from stepping on wrong symbols
    Generally seen in numpy.

    Warning: if both .a and .so are built, this could hide symbols.
    TODO: actually make sure that -l<lib> is used in the build line for .so, and only then use it
    """
    global BLACKLIST

    if BLACKLIST is not None:  # calculate it only once...
        return BLACKLIST

    logging.info(f"Checking {location} for blacklisted symbols")
    libs = [os.path.join(path, file)
            for path, _, files in os.walk(location)
            for file in files if file.endswith('.a') and not file.endswith('km.lib.a')]
    if len(libs) > 0:
        nm = subprocess.run(["nm", "-A", "--extern-only", "--defined-only"] + libs,
                            capture_output=True, encoding="utf-8")
        if (nm.returncode != 0):
            logging.warning(f"can't form blacklist, nm failed: {nm.stderr}")
            return None
        logging.info(f"blacklisting symbols in libs: {libs}")
        BLACKLIST = [i.split()[2] for i in nm.stdout.splitlines()
                     if i and not i.endswith(":")]
    else:
        BLACKLIST = []
    return BLACKLIST

def save_c_tables(so_full_path, so_id, symbols, suffix=so_suffix):
    """
    Write C file with init symtables
    """
    file_name = so_full_path.replace(suffix, symbols_c_suffix)
    logging.info(f"Generating {file_name}")
    with open(file_name, 'w') as f:
        template = jinja2.Template(symfile_template)
        f.write(template.render(so=os.path.basename(so_full_path),
                                so_id=so_id,
                                symbols=symbols))
    try:  # beautify the freshly created .c file
        cl = subprocess.run(["clang-format", "-i", "-style=file",
                             file_name], capture_output=False, encoding="utf-8")
        if cl.returncode != 0:
            logging.warning(f"C code will not be formatted - clang-format has failed. File {file_name}")
    except FileNotFoundError:
        logging.warning(f"C code will not be formatted - clang-format was not found. File {file_name}")


def save_artifacts(meta_data, symbols, location):
    """
    Writes out generated files for a specific .so
    Throws if issues with file open/write.
    """
    so_file_name = meta_data["so"]
    so_full_path = os.path.join(location, so_file_name)
    so_name_munged = str(meta_data["so_name_munged"])

    # Save symbols munging info, for use with objcopy
    with open(so_full_path.replace(so_suffix, symmap_suffix), 'w') as f:
        f.writelines([f"{s['name']} {s['name_munged']}\n" for s in symbols])
    # generate C file with sym tables for dlsym
    save_c_tables(so_full_path, so_id=so_name_munged, symbols=symbols)
    # safe the file with unique id for each .so - we need it for finding registrations during dlopen()
    with open(so_full_path + id_suffix, 'w') as f:
        f.write(so_name_munged)


def good_l_flag(flag):
    """
    returns True if flag is a '-l' flag we need to handle
    """
    return flag.startswith('-l') and not flag.startswith('-lpython') and not flag == '-ldl'


def nm_get_symbols(location, so_file_name, extra_flags=["--dynamic"]):
    """
    Get symnames from 'nm'-produced output, i.e.a list of "address TYPE symname" lines.
    """
    logging.info(f"nm {so_file_name}")
    so_full_path_name = os.path.join(location, so_file_name)
    nm = subprocess.run(["nm", "--extern-only", "--defined-only"] + extra_flags + [so_full_path_name],
                        capture_output=True, encoding="utf-8")
    if nm.returncode != 0:
        logging.warning(f"Failed to get names for {so_full_path_name}")
        return None
    symbols = [i.split()[2] for i in nm.stdout.splitlines()]
    return symbols

SYSTEM_LIB_PATHS=["/usr/lib", "/usr/local/lib", "/usr/lib64", "/lib", "/lib64"]
def strip_system_lib_paths(paths):
   """
   Removes system libraries paths from passed paths array.
   """
   # print(f"paths: {paths}")
   if paths == None:
      return None
   return [p for p in paths if p not in SYSTEM_LIB_PATHS ]

def process_line(line, location, skip_list):
    """
    Given a 'line' used to build an .so, analyzes it and writes all file necessary to build linkable
    .a for this .so. Notice that the actual build of .a will be done with a generated makefile, so here we
    just need to generate .c file with symname mapping, nd the map table to munge obj files,
    to guarantee uniqueness during static link phase.

    'location' is where all relative paths are starting from
    """
    items = line.split()
    # extend '@' files:
    try:
        extra_files = [i[1:] for i in items if i.startswith('@')]
        for f in extra_files:
            logging.info(f"   Adding items from {f}")
            with open(f, 'r') as file:
                items += file.read().split('\n')
    except:
        pass  # ignore failures

    try:
        so_file_name = [i for i in items if i.endswith(so_suffix) and not i.startswith('-')][0]
    except:
        logging.warning(f"No.so files in line: '{items}'")
        return None
    # check if the module we are looking at is on skip list
    basename = os.path.basename(so_file_name)
    try:
        basename = basename[:basename.index('.')]
    except:
        pass  # no '.' in name, we are good
    if basename in skip_list:
        return None
    objs = [i for i in items if i.endswith(".o")]
    so_full_path_name = os.path.join(location, so_file_name)
    id, so_name_munged = convert(so_full_path_name)
    meta_data = {"so": so_file_name, "id": id, "so_name_munged": so_name_munged, "objs": objs,
                 "ldflags": [i for i in items if good_l_flag(i)],
                 "ldpaths": strip_system_lib_paths([i[2:] for i in items if i.startswith("-L")])}
    symbols = [{"name": n, "name_munged": n + id if need_to_mung else n}
               for n in nm_get_symbols(location, so_file_name) if n not in sym_blacklist(location) and n.find('.') == -1]
    save_artifacts(meta_data, symbols, location)
    return meta_data


def process_file(file_name, skip_list):
    """
    Analyze a file with build (cc) log, finds commands used to build.so, and
    generates necessary files per .so, and the makefile to build artifacts for static link.
    skip_list is a list of .so names (sans .so suffix) to skip.
    """
    location = os.path.realpath(os.path.dirname(file_name))
    with open(file_name, 'r') as f:
        lines_with_so = [l for l in f.readlines() if so_pattern.match(l)]

    if len(lines_with_so) == 0:
        logging.warning(f"{so_suffix} is not mentioned in {file_name} - nothing to do.")
        return
    # accumulate data for makefile generation, and generate it
    mk_info = [process_line(line, location, skip_list) for line in lines_with_so]
    # clear None's
    mk_info = [x for x in mk_info if x]
    # get ldflags from mkinfo as list of lists, and glue them together in one
    all_l_flags = reduce(lambda x, y: x + y, [i["ldflags"] for i in mk_info])
    all_l_pathes = reduce(lambda x, y: x + y, [i["ldpaths"] for i in mk_info])

    # remove duplicates and also remove cross-references to libs in the same package
    # Get the list of libs in this package by converting /path/libNAME.so to NAME
    already_in = ["-l" + os.path.splitext(os.path.basename(f["so"]))[0][3:] for f in mk_info]
    final = []
    for i in all_l_flags:
        if i not in final and i not in already_in:
            final.insert(0, i)
    finaL = []
    for i in all_l_pathes:
        if i not in finaL:
            finaL.insert(0, i)
    ldpaths = " ".join([f"-L{os.path.join(location, i)}" for i in finaL])
    logging.info(f"Final -llist: {final}")
    mk_name = os.path.join(location, "dlstatic_km.mk")
    with open(mk_name, 'w') as f:
        template = jinja2.Template(makefile_template)
        f.write(template.render(libs=[i["so"] for i in mk_info],
                                info=mk_info,
                                symmap_suffix=symmap_suffix,
                                ldflags=" ".join(final),
                                ldpaths=ldpaths,
                                echo_if_no_mung="" if need_to_mung else "true"))
    # Write all meta data used for makefile creation - for debug and log purposes
    mk_json_name = os.path.join(location, "dlstatic_km.mk.json")
    with open(mk_json_name, 'w') as f:
        f.write(json.dumps(mk_info, indent=3))
    print(f"Log analysis completed. To build, use this command:\nmake -C {location} -f {os.path.basename(mk_name)}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Process .so build log and generate files for static dlopen.")
    parser.add_argument('build_out_file', type=argparse.FileType('r'),
                        help='File with build output, need to have .so build lines')
    parser.add_argument('--skip', type=argparse.FileType('r'), action='store',
                        help='A file with new line separated names of modules to skip in the package being analyzed')
    parser.add_argument('--self', action="store_true",
                        help='[WIP] Generate tables for dlopen(NULL). build_out_file is .km file, e.g. python.km')
    parser.add_argument('--no_mung', action="store_true",
                        help='Skip symnames munging and use symbols as is ')
    parser.add_argument('--log', action="store", choices=['verbose', 'quiet'],
                        help='Logging level. Verbose prints all info. Quiet only prints errors. Default is errors and warnings')
    args = parser.parse_args()
    file_name = args.build_out_file.name
    if not args.log:
        logging.basicConfig(level=logging.WARNING)  # default
    else:
        if args.log == 'verbose':
            logging.basicConfig(level=logging.INFO)
        else:  # 'quiet'
            logging.basicConfig(level=logging.CRITICAL)
    logging.info(f"Analyzing {file_name}")
    skip_list = ""
    if (args.skip):
        skip_list = [l for l in args.skip.read().split('\n') if not l.startswith('#') and len(l) > 1]
        logging.info(f"Skipping list: {skip_list}")
    if args.no_mung:
        need_to_mung = False
    if args.self:
        logging.info(f" build dlopen(null) tables for {file_name} in {os.path.curdir}")
        symbols = [{"name": n, "name_munged": n}
                   for n in nm_get_symbols(os.path.curdir, file_name, extra_flags=[])]
        logging.info(f"symlen {len(symbols)}")
        save_c_tables(os.path.realpath(file_name), os.path.basename(file_name), symbols, suffix=".km")
    else:
        process_file(file_name, skip_list)
