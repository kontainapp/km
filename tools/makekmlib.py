#!/usr/bin/env python3

import argparse
import subprocess
from os.path import basename,dirname
from tempfile import NamedTemporaryFile

def get_exports(file):
   '''
   Gets list of exported symbols from an object file
   '''
   return [line.split()[-1] for line in subprocess.getoutput(
            'readelf -s {} | grep "GLOBAL *DEFAULT *[0-9]"'
             .format(file)).split('\n')]

def translate_exports(lib, syms):
   return { sym : '{}_{}'.format(sym, lib) for sym in syms}

def build_lib_bundle(args):
   file = args.file[0]
   filebase = basename(file).rsplit('.', 2)[0]
   dir = dirname(file)
   if len(dir) == 0:
      dir = '.'
   renamed_obj = '{}/km_obj_{}'.format(dir, basename(file))
   linkage_src = '{}/km_lnk_{}.c'.format(dir, filebase)
   linkage_obj = '{}/km_lnk_{}.o'.format(dir, filebase)
   library_obj = '{}/km_lib_{}.o'.format(dir, filebase)

   # Build the translation structure
   t = translate_exports(filebase, get_exports(file))

   # Generate rename commands file
   oc_rename = NamedTemporaryFile()
   f = open(oc_rename.name, 'w')
   for sym,tsym in t.items():
      f.write('{} {}\n'.format(sym, tsym))
   f.close()

   # Run objcopy to rename symbols
   cmd = 'objcopy --redefine-syms {} {} {}'.format(oc_rename.name, file, renamed_obj).split()
   subprocess.run(cmd)

   # Generate runtime translation table for KM
   f = open(linkage_src, 'w')
   f.write('/* Do not edit - this file is auto-generated */\n')
   f.write('#include "km_dl_linkage.h"\n')
   f.write('#include <stddef.h>\n')
   for sym,tsym in t.items():
      f.write('extern void * {};\n'.format(tsym))

   f.write('km_dlsymbol_t _{}_syms[] = {{\n'.format(filebase))
   for sym,tsym in t.items():
      f.write('  {{.sym_name = "{}", .sym_addr = &{} }},\n'.format(sym, tsym))
   f.write('  {.sym_name = NULL, .sym_addr = NULL },\n')
   f.write('};\n')
   f.close()

   # Compile linkage table
   TOP = subprocess.getoutput('git rev-parse --show-cdup').split()[0]
   print(TOP)
   cmd = 'cc -c -o {} {} -I {}include'.format(linkage_obj, linkage_src, TOP).split()
   subprocess.run(cmd)

   # link into one binary
   cmd = 'ld -o {} --relocatable {} {}'.format(library_obj, linkage_obj, renamed_obj).split()
   subprocess.run(cmd)


if __name__ == '__main__':
   parser = argparse.ArgumentParser()
   parser.add_argument('file', nargs=1)
   args = parser.parse_args()


   build_lib_bundle(args)
