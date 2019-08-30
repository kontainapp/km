#!/usr/bin/env python3

import argparse
import subprocess
import re

def Main(args):
   print('output={} args={}'.format(args.o, args.files))
   c = [f for f in args.files if re.match(r'km_lib_.*', f)]
   print(c)

if __name__ == '__main__':
   parser = argparse.ArgumentParser()
   parser.add_argument('-o', nargs=1)
   parser.add_argument('files', nargs='+')
   args = parser.parse_args()

   Main(args)
