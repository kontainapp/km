#!/bin/bash -e
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
# Creates a release kontain.tar.gz for uploading to github. To unpackage, 'tar -C /opt/kontain -xvf kontain.tar.gz'
#
[ "$TRACE" ] && set -x

cd "$( dirname "${BASH_SOURCE[0]}" )"

BLDTOP="$(realpath ../../build)"
readonly TARBALL=${BLDTOP}/kontain.tar

rm -f $TARBALL $TARBALL.gz

# package by doing `tar -C locations[i] files[i]`
declare -a locations; locations=(/opt/kontain ../..        ../..               ../../tools)
declare -a files ;    files=(.                docs/release tests/hello_test.km bin)

for i in $(seq 0 $(("${#locations[@]}" - 1)) ) ; do
   source="${locations[$i]}/${files[$i]}"
   decompress_list=`find $source -type f | xargs file  | egrep '(shared|archive)' | awk -F: '{print $1}'`
   if [ -n "$decompress_list" ] ; then echo Decompressing .debug_info for `echo $decompress_list | wc -w` files ; fi
   for file in $decompress_list
   do
      objcopy --decompress-debug-sections $file
   done
   echo "Packaging ${source}"
   tar -C ${locations[$i]} -rf $TARBALL ${files[$i]}
done

echo "Zipping $TARBALL.gz ..."
gzip $TARBALL

ls -lh ${TARBALL}*

