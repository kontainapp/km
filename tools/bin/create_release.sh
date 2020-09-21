#!/bin/bash
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
# Creates a release kontain.tar.gz for uploading to github. To unpackage, 'tar -C /opt/kontain -xvf kontain.tar.gz'
#
set -e ; [ "$TRACE" ] && set -x

cd "$( dirname "${BASH_SOURCE[0]}" )"

BLDTOP="$(realpath ../../build)"
readonly TARBALL=${BLDTOP}/kontain.tar
readonly OPT_KONTAIN_TMP=${BLDTOP}/opt_kontain

rm -fr $TARBALL $TARBALL.gz $OPT_KONTAIN_TMP
# we may need to modify all obj file so make sure we work on a copy
cp -rf /opt/kontain $OPT_KONTAIN_TMP

# package by doing `tar -C locations[i] files[i]`
declare -a locations; locations=($OPT_KONTAIN_TMP ../..        ../..              ../../tools ../../tools/faktory )
declare -a files ;        files=(.           docs/release tests/hello_test.km   bin           bin )

for i in $(seq 0 $(("${#locations[@]}" - 1)) ) ; do
   source="${locations[$i]}/${files[$i]}"
   decompress_list=$(find $source -type f -exec file '{}' ';' |  awk -F: '/(shared|archive|relocatable)/ {print $1}')
   if [ -n "$decompress_list" ] ; then
      echo Decompressing .debug_info for `echo $decompress_list | wc -w` files in $source
      if [ ! -w $source ] ; then
         echo WARNING: No write access to $source - objcopy will need to touch files.
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

