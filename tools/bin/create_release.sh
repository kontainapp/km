#!/bin/bash
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
# Creates a release kontain.tar.gz for uploading to github. To unpackage, 'tar -C /opt/kontain -xvf kontain.tar.gz'
#
# May need to run as a `sudo` because objcopy modifies files in /opt/kontain
#
set -e ; [ "$TRACE" ] && set -x

cd "$( dirname "${BASH_SOURCE[0]}" )"

BLDTOP="$(realpath ../../build)"
readonly TARBALL=${BLDTOP}/kontain.tar

rm -f $TARBALL $TARBALL.gz

# package by doing `tar -C locations[i] files[i]`
declare -a locations; locations=(/opt/kontain ../..        ../..              ../../tools ../../tools/faktory )
declare -a files ;        files=(.           docs/release tests/hello_test.km   bin           bin )

for i in $(seq 0 $(("${#locations[@]}" - 1)) ) ; do
   source="${locations[$i]}/${files[$i]}"
   decompress_list=$(find $source -type f -exec file '{}' ';' |  awk -F: '/(shared|archive|relocatable)/ {print $1}')
   if [ -n "$decompress_list" ] ; then
      echo Decompressing .debug_info for `echo $decompress_list | wc -w` files in $source
      if [ ! -w $source ] ; then
         echo WARNING: No write access to $source - objcopy will need to touch files.  May need to run as sudo
      fi
   fi

   for f in $decompress_list
   do
      objcopy --decompress-debug-sections --preserve-dates $f
   done

   echo "Packaging ${source}"
   tar -C ${locations[$i]} -rf $TARBALL ${files[$i]}
done

echo "Zipping $TARBALL.gz ..."
gzip $TARBALL

