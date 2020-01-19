#!/bin/bash
#
# Link python.km
#
# always picks up .a (converted from .so) mentioned in linkbasic.txt
#
# $1 is location where cpython artifacts are
# $2 is where to put the results of the link
# $3 is optional space separated list of modules to link in (e.g. "numpy markupsafe")
#
set -e
[ "$TRACE" ] && set -x

cd $(dirname ${BASH_SOURCE[0]})
KM_TOP=$(git rev-parse --show-cdup)
PATH=$(realpath ${KM_TOP}/tools):$PATH

BUILD=$(realpath ${1:-cpython})
OUT=$(realpath ${2:-cpython})
ext_file=$(realpath extensions/modules.txt)

#default_modules="greenlet pyrsistent"
default_modules=""
# always pick up ctypes and other embedded.
# link_files=@$(realpath linkbasic.txt)
link_files=@$(realpath cpython/linkline_km.txt)

modules_to_link="$default_modules $(awk '/^[^#]/ {print $1}' $ext_file)"
modules_loc=$(realpath cpython/Modules)
for name in $modules_to_link ; do
   ld_info=${modules_loc}/${name}/linkline_km.txt
   if [[ -f $ld_info ]] ; then
      link_files="${link_files} @$ld_info"
   else
      echo "*** Warning: $name is not linked in - missing '${ld_info}'"
   fi
done

cd cpython; pwd; kontain-gcc -ggdb ${BUILD}/Programs/python.o \
   ${link_files} \
   ${BUILD}/libpython3*.a -lz -lssl -lcrypto $LDLIBS \
   -o  ${OUT}/python.km && chmod a-x  ${OUT}/python.km

