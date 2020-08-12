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
declare -a files ;    files=(.                docs/product tests/hello_test.km bin)

for i in $(seq 0 $(("${#locations[@]}" - 1)) ) ; do
   echo "Packaging ${locations[$i]}/${files[$i]}"
   eval "tar -C ${locations[$i]} -rf $TARBALL ${files[$i]}"
done

echo "Zipping $TARBALL.gz ..."
gzip $TARBALL

ls -lh ${TARBALL}*

