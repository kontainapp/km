#!/bin/bash
#
# prep dlopen/dlsym tables for a module from bear.out
# set -x
output=bear.out
exedir=$(dirname "${BASH_SOURCE[0]}")
so_suffix=$(python3-config --extension-suffix)

grep $so_suffix $output | while read line
do
   so=$(echo $line | tr ' ' '\n' | grep $so_suffix)
   mod_name=$(basename $so | sed "s/$so_suffix//")
   out=${mod_name}_table.c
   nm  -s --defined-only -Dg $so | awk "$(m4 -DMODULE=$mod_name $exedir/awk_script)" > $out
done
