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
# This extracts a shared object build info that was captured with 'bear' into 'compile_commands.json' and bear.out, and then
# converts it into format acceptable by Setup.local, and a helper files with info for linker
#
import json
import sysconfig
import re
import sys
import os
import hashlib

# setup.py puts binary builds here.
BUILD_LIB_PREFIX = "build/lib.{}-{}/".format(sysconfig.get_platform(),
                                             sysconfig.get_python_version())
# setup.py-generated source (if any) goes here - we'd need to remove it from "synthetic" buildin module name
BUILD_SRC_PREFIX = BUILD_LIB_PREFIX.replace("build/lib.", "build/src.")
# .so suffix, per 'python3-config --extension-suffix'. VERSION DEPENDANT !
EXTENSION_SUFFIX = ".cpython-37m-x86_64-linux-gnu.so"


def strip_eq1(opt):
    """
    Strip '=1' from -D flags.
    1 is default anyways, and '=' confuses python setup.local conversions.
    """
    if not opt.startswith('-D'):
        raise Exception("opt is not -D flag")
    if opt.endswith('=1'):
        opt = opt[:opt.rindex('=1')]
    return opt


def read_buildinfo(compile_commands_file='compile_commands.json'):
    """
    Reads files produced by 'bear python3 setup.py build > bear.out' for a module,
    and places results in internal dicts.
    Returns the json object with build info or None if no compiled extensions are found
    """
    with open(compile_commands_file, 'r') as f:
        compiles = json.load(f)

    sources = dict()
    for cmd in compiles:
        objs = [opt for opt in cmd['arguments'] if opt.endswith('.o')]
        if len(objs) != 1:
            raise Exception('should have a single object')
        sources[objs[0]] = {'source': cmd['file'],
                            'dopts': [strip_eq1(opt) for opt in cmd['arguments'] if opt.startswith('-D')],
                            'uopts': [opt for opt in cmd['arguments'] if opt.startswith('-U')],
                            'iopts': [opt for opt in cmd['arguments'] if opt.startswith('-I')],
                            }
    with open("bear.out", 'r') as f:
        output = [l for l in f.readlines() if l.find(EXTENSION_SUFFIX) != -1]
    if not output:
        print("No .so found in build output, assuming .py only module in ", os.getcwd())
        return None

    libs = list()
    for line in output:
        link_args = line.split()
        ldopts = [f for f in link_args if f.startswith(
            '-l') and not f.startswith('-lpython')]
        libpath = [f for f in link_args if f.startswith('-L')]
        objs = [f for f in link_args if f.endswith('.o')]
        so = [f for f in link_args if f.endswith('.so')]
        if len(so) != 1:
            raise Exception('expected one .so file. Found {}', len(so))
        libs.append({'name': so[0], 'objects': objs,
                     'ldopts': ldopts, 'libpath': libpath,
                     'sources': [sources[obj] for obj in objs]})
    return libs


def update_source_file(file_name, pkg, munged_modname):
    """
    if file contains PyInit_*, rewrite it to new (unique) name, on the way changing the PyInit function name to include package path,
    e.g. for falcon.media package, handlers.c module, ww will change PyInit_handlers() -> PyInit__KM_falcon_media_handlers()

    Unfortunately, any source file (even with no PyInit calls) can generate a conflict since all .o are piled in Modules.
    To avoid that we change all names by adding path hash - otherwise we would need to track file names ACROSS ALL MODULES se we can munge them on clashes.

    This function returns the new name.
    """

    new_file_name = os.path.join(os.path.dirname(file_name),
                                 "{module}_{digest}-{file}".format(digest=hashlib.md5(file_name.encode()).hexdigest(),
                                                                   module=pkg.split('.')[0], file=os.path.basename(file_name)))
    if os.path.isfile(new_file_name):  # if file already exists, nothing to do. Use 'git clean -xdf' to rebuild
        return new_file_name

    init_func = 'PyInit_' + pkg.split('.')[-1]
    km_init_func = 'PyInit_' + munged_modname
    with open(file_name, 'r') as f:
        content = f.read()
    with open(new_file_name, 'w') as new_file:
        new_file.write(content.replace(init_func, km_init_func))
    return new_file_name


def extract_opts(opt_type, sources):
    """
    # extract all individual options (e.g 'dopts') for a .so source file(s) from nested lists
    # into a single list, dedup entries with 'set()' conversion and return de-duped list.
    # note that set() can change order in the list
    """
    all_opts = [val for options in [opt_info[opt_type]
                                    for opt_info in sources] for val in options]
    return list(set(all_opts))


def convert_buildinfo_to_setup(libs,
                               setup_info_file='km_setup.local',
                               libs_info_file='km_libs.json',
                               libs_cmdinfo_file="km_libs.txt"):
    """
    Converts build info in libs to format understood by Python's Setup.local, and save results in km_setup.local
    Also dumps km_libs.json with -l / -L info for the build.
    Returns 0 when conversion is done, 1 when skipped.
    """

    if not libs:
        return 1

    base = os.path.basename(os.getcwd())
    total_L = list()
    total_l = list()
    f_setup_local = open(setup_info_file, 'w')
    for lib in libs:
        name = lib['name']
        # convert full .so name (e.g. 'build/lib.linux-x86_64-3.7/falcon/version.cpython-37m-x86_64-linux-gnu.so')
        # to KM specific "munged" module name which includes package hierarchy (e.g. _KM_falcon_version)
        # pkg includes package hierarchy and module name, e.g. 'falcon.version'
        pkg = name[len(BUILD_LIB_PREFIX):name.rindex(EXTENSION_SUFFIX)].replace('/', '.')
        munged_modname = '_KM_{}'.format(pkg.replace('.', '_'))
        print("pkg: ", pkg, ' munged to ', munged_modname, " for name=", name)
        #   print("ORIGIN lib['sources']=", lib['sources'])
        dopts = extract_opts('dopts', lib['sources'])
        uopts = extract_opts('uopts', lib['sources'])
        iopts = [o for o in extract_opts('iopts', lib['sources']) if not o.startswith('-I/')]

        # Generate km_<file>.c with PyInit_<mangledModule> from source files with PyInit_<module> definitions.
        # Package hierarchy will be reflected in target file name and module name (see below)
        #
        # libs example: 'sources': [{'source': 'falcon/util/uri.c', 'dopts': ['-DDYNAMIC_ANNOTATIONS_ENABLED', '-DNDEBUG', '-D_GNU_SOURCE'],
        #                           'uopts': [], 'iopts': ['-I/usr/include/python3.7m']}]
        #
        srcs = list()
        for source in lib['sources']:
            srcs.append(update_source_file(source['source'], pkg, munged_modname))
        uflags = uopts
        iflags = ["-IModules/{}/{}".format(base, x[2:]) for x in iopts]  # ':2' drops '-I'
        sources = ["{}/{}".format(base, x) for x in srcs]
        Lflags = ["-LModules/{}/{}".format(base, x[2:]) for x in lib['libpath'] if not x.startswith("-L/")]
        lflags = lib['ldopts']
        line_elements = [munged_modname] + sources + dopts + uflags + iflags + Lflags + lflags
        f_setup_local.write(" ".join(line_elements) + "\n")
        # Collect -L and -L for link-km.sh
        total_L += Lflags
        total_l += lflags
    f_setup_local.close()

    # dedup entries
    total_L = list(dict.fromkeys(total_L))
    total_l = list(dict.fromkeys(total_l))
    if len(total_l):
        with open(libs_info_file, 'w') as f:  # json dump
            json.dump({'L': total_L, 'l': total_l}, f, indent=4)
        with open(libs_cmdinfo_file, 'w') as f:  # LDFLAGS-style dump
            f.writelines(" ".join(total_L + total_l) + "\n")

    return 0


if __name__ == '__main__':
    sys.exit(convert_buildinfo_to_setup(read_buildinfo()))
