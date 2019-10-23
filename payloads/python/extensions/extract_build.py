#!/bin/env python3

# This extracts a shared object build that was captured with 'bear'
# into 'compile_commands.json' and the stdout captured
# into 'bear.out'. This merges the data to capture the
import json
import sysconfig
import re
import sys
import os
from os.path import basename, dirname

# setup.py puts builds here. it is extra
BUILD_PREFIX = "build/lib.{}-{}/".format(sysconfig.get_platform(),
                                         sysconfig.get_python_version())
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
        objs = [opt for opt in cmd['arguments'] if re.match(r'.*\.o', opt)]
        if len(objs) != 1:
            raise Exception('should have a single object')
        sources[objs[0]] = {'source': cmd['file'],
                            'dopts': [strip_eq1(opt) for opt in cmd['arguments'] if re.match(r'-D.*', opt)],
                            'uopts': [opt for opt in cmd['arguments'] if re.match(r'-U.*', opt)],
                            'iopts': [opt for opt in cmd['arguments'] if re.match(r'-I.*', opt)],
                            }

    with open("bear.out", 'r') as f:
        output = [l for l in f.readlines() if l.find(EXTENSION_SUFFIX) != -1]

    if not output:
        print("No .so found in build output, assuming .py only module in ", os.getcwd())
        return None

    libs = list()
    for line in output:
        # ldopts = [f for f in line.split() if re.match(r'-Wl.*', f)]
        ldopts = [f for f in line.split() if re.match(
            r'-l.*', f) and not re.match(r'-lpython*', f)]
        libpath = [f for f in line.split() if re.match(r'-L.*', f)]
        objs = [f for f in line.split() if re.match(r'.*\.o', f)]
        so = [f for f in line.split() if re.match(r'.*\.so', f)]
        if len(so) != 1:
            raise Exception('expected one .so file. Found {}', len(so))
        libs.append({'name': so[0], 'objects': objs,
                     'ldoptions': ldopts, 'libpath': libpath,
                     'sources': [sources[obj] for obj in objs]})
    return libs


def convert_buildinfo_to_setup(libs, setup_info_file='km_setup.local', libs_info_file='km_libs.json'):
    """
    Converts build info in libs to format understood by Python's Setup.local, and save results in km_setup.local
    Also dumps km_libs.json with -l / -L info for the build.
    Returns 0 when conversion is done, 1 when skipped.
    """

    if not libs:
        return False

    with open(setup_info_file, 'w') as f_setup_local:
        base = os.path.basename(os.getcwd())
        total_L = total_l = set()
        # setup.py puts builds here. it is extra
        BUILD_PREFIX = "build/lib.{}-{}/".format(sysconfig.get_platform(),
                                                 sysconfig.get_python_version())
        # .so suffix, per 'python3-config --extension-suffix'. VERSION DEPENDANT !
        EXTENSION_SUFFIX = ".cpython-37m-x86_64-linux-gnu.so"

        for lib in libs:
            # convert full .so name (e.g. 'build/lib.linux-x86_64-3.7/falcon/version.cpython-37m-x86_64-linux-gnu.so')
            # to KM specific "munged" module name which includes package hierarchy (e.g. _KM_falcon_version)
            name = lib['name']
            # pkg included package hierarchy and module name, e.g. 'falcon.version'
            pkg = name[len(BUILD_PREFIX):name.rindex(
                EXTENSION_SUFFIX)].replace('/', '.')
            munged_modname = '_KM_{}'.format(pkg.replace('.', '_'))
            print("pkg: ", pkg, ' munged to ', munged_modname)
            dopts = uopts = iopts = set()
            srcs = list()
            for source in lib['sources']:
                # For source files with PyInit_<module>, definition, generate km_<file>.c with PyInit_<mangledModule>
                # Package hierarchy will be reflected in target file name and module name (see below)
                #
                # libs example: 'sources': [{'source': 'falcon/util/uri.c', 'dopts': ['-DDYNAMIC_ANNOTATIONS_ENABLED', '-DNDEBUG', '-D_GNU_SOURCE'], 'uopts': [], 'iopts': ['-I/usr/include/python3.7m']}]
                #
                name = source['source']
                with open(name, 'r') as f:
                    init_func = 'PyInit_' + pkg.split('.')[-1]
                    km_init_func = 'PyInit_' + munged_modname
                    content = f.read()
                    if content.find(init_func) != -1:
                        # convert call to PyInit, e.g.  PyInit_handlers() -> PyInit__KM_falcon_media_handlers()
                        new_content = content.replace(init_func, km_init_func)
                        # convert file name, e.g falcon/media/handlers.c ->falcon/media/km_falcon_media_handlers.c
                        new_name = name.replace(basename(name), 'km_' + dirname(
                            name).replace('/', '_') + '_' + basename(name))
                        with open(new_name, 'w') as new_f:
                            new_f.write(new_content)
                        srcs.append(new_name)
                    else:
                        srcs.append(name)

                for opt in source['dopts']:
                    dopts.add(opt)

                for opt in source['uopts']:
                    uopts.add(opt)

                for opt in source['iopts']:
                    if re.match(r'-I/.*', opt):
                        continue
                    iopts.add(opt)

            dflags = ''.join(' {}'.format(x) for x in dopts)
            uflags = ''.join(' {}'.format(x) for x in uopts)
            iflags = ''.join(' -IModules/{}/{}'.format(base, re.sub(r'-I', '', x))
                             for x in iopts)
            sources = ''.join(' {}/{}'.format(base, x) for x in srcs)
            Lflags = [re.sub(r'-L', '', x)
                      for x in lib['libpath'] if not re.match(r'-L/.*', x)]
            Lstr = ''.join(' -LModules/{}/{}'.format(base, Lflags))
            if len(Lflags) == 0:
                Lstr = ''
            lflags = ''.join(' {}'.format(x) for x in lib['ldoptions'])
            f_setup_local.write('{}{}{}{}{}{}{}\n'.format(munged_modname, sources, dflags,
                                                          uflags, iflags, Lstr, lflags))

            # Collect -L and -L for link-km.sh
            for l in Lflags:
                total_L.add(l)
            for l in lib['ldoptions']:
                total_l.add(l)

    # write '-l' and '-L' flags info
    with open(libs_info_file, 'w') as f:
        json.dump({'L': list(total_L), 'l': list(total_l)}, f, indent=4)

    return True


if __name__ == '__main__':
    sys.exit(convert_buildinfo_to_setup(read_buildinfo()))
