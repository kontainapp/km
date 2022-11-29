#!/bin/bash
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
#
# Creates a release kontain.tar.gz for uploading to github. To unpackage, 'tar -C /opt/kontain -xvf kontain.tar.gz'
#
set -e
[ "$TRACE" ] && set -x

cd "$(dirname "${BASH_SOURCE[0]}")"

TOP="$(realpath ../..)"
BLDTOP=$TOP/build
readonly TARBALL=${BLDTOP}/kontain.tar
readonly OPT_KONTAIN=${BLDTOP}/opt/kontain
readonly OPT_KONTAIN_TMP=${BLDTOP}/opt/kontain_tmp

echo "Creating temporary directory"
mkdir -p ${OPT_KONTAIN_TMP}/bin

# we may need to modify all obj file so make sure we work on a copy
cp -rf --preserve=links ${OPT_KONTAIN}/alpine-lib ${OPT_KONTAIN_TMP}
cp -rf --preserve=links ${OPT_KONTAIN}/bin ${OPT_KONTAIN_TMP}
cp -rf --preserve=links ${OPT_KONTAIN}/include ${OPT_KONTAIN_TMP}
cp -rf --preserve=links ${OPT_KONTAIN}/lib ${OPT_KONTAIN_TMP}
cp -rf --preserve=links ${OPT_KONTAIN}/runtime ${OPT_KONTAIN_TMP}

# copy krun
cp -f --preserve=links ${TOP}/container-runtime/crun/krun.static ${OPT_KONTAIN_TMP}/bin/krun
ln -f ${OPT_KONTAIN_TMP}/bin/krun ${OPT_KONTAIN_TMP}/bin/krun-label-trigger

# include docker and podman config scripts in the tarball
cp ${TOP}/container-runtime/{podman,docker}_config.sh ${OPT_KONTAIN_TMP}/bin
cp ${TOP}/container-runtime/{podman,docker}_config.sh ${OPT_KONTAIN}/bin # for kontain_bin.tar ( since _TMP wil be deleted)
cp ${TOP}/km-releases/kontain-install.sh ${OPT_KONTAIN_TMP}/bin
cp ${BLDTOP}/cloud/k8s/shim/containerd-shim-krun-v2 ${OPT_KONTAIN_TMP}/bin
cp ${BLDTOP}/cloud/k8s/shim/containerd-shim-krun-label-trigger-v2 ${OPT_KONTAIN_TMP}/bin

# package by doing `tar -C locations[i] files[i]`
declare -a locations
declare -a files
declare -a exclude
# For each location copy related files to the destination (/opt/kontain).
locations=($OPT_KONTAIN_TMP $TOP $TOP/km-releases)
files=(. tests/hello_test.km examples)

for i in $(seq 0 $(("${#locations[@]}" - 1))); do
   source="${locations[$i]}/${files[$i]}"
   # newer gcc produces compressed '.debug_info'. Older linkers cannot use it. To make sure
   # we can use our (apine) libs with slightly older linkers, let's decompress .debug_info
   decompress_list=$(find $source -type f -exec file '{}' ';' | grep -v 'archive data' |
                     awk -F: '/(shared|archive|relocatable)/ {print $1}')
   if [ -n "$decompress_list" ]; then
      echo Decompressing .debug_info for $(echo $decompress_list | wc -w) files in $source
      if [ ! -w $source ]; then
         echo WARNING: No write access to $source - objcopy will need to touch files.
      fi
   fi

   for f in $decompress_list; do
      objcopy --decompress-debug-sections --preserve-dates $f
   done

   echo "Packaging ${source}"
   tar -C ${locations[$i]} -rf $TARBALL ${files[$i]}
done

echo "Zipping $TARBALL.gz ..."
gzip ${TARBALL}

echo "Cleaning up"
rm -rf ${OPT_KONTAIN_TMP}