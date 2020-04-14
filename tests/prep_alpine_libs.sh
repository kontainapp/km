#!/bin/bash -e
#
# Prep alpine libs in TARGET_LIB_ALPINE for packaging into buildenv image
# by building alpine container image with all the goodies and extracting stuff from there.
#
# Also prep symlinks in TARGET_LIB_ALPINE (TODO - use em)
#
# usage: ./prep_alpine_libs [repo_top] [alpine_libs_image] [TARGET_LIB_ROOT]
#
[ "$TRACE" ] && set -x

TOP=$(realpath $(dirname "${BASH_SOURCE[0]}"))

TARGET_LIB_ROOT=${1:-/opt/kontain}
TARGET_LIB_ALPINE=${TARGET_LIB_ROOT}/alpine-lib
TARGET_LIB_KONTAIN=${TARGET_LIB_ROOT}/runtime

TMP_LIBS_IMAGE=kontain/alpine-lib-tmp

# use this version of alpine image
FROM_ALPINE=alpine:3.11.5

if [ ! -w ${TARGET_LIB_ALPINE} ] ; then
   sudo sh -c "mkdir -p ${TARGET_LIB_ALPINE}; chmod 777 ${TARGET_LIB_ALPINE}"
fi

cd ${TOP}

# Build alpine containers with necessary static libs
d=$(mktemp -d)  # temp empty dir to make docker build faster
echo "Building ${TMP_LIBS_IMAGE} based on instructions in alpine-lib.dockerfile..."
docker build --build-arg=FROM_IMAGE=$FROM_ALPINE -t ${TMP_LIBS_IMAGE} -f alpine-lib.dockerfile $d
rmdir $d

### extract alpine bits
container=$(docker create ${TMP_LIBS_IMAGE})
echo "Exporting libs to ${TARGET_LIB_ALPINE}..."
docker export $container | \
   tar -C ${TARGET_LIB_ALPINE}  -xf - 'lib/*so*' usr/lib/lib\* '*.o' 'usr/lib/gcc/*/*/*.[oa]'
docker rm $container
# deliberatly keeping it around in cache
# docker rmi ${TMP_LIBS_IMAGE}

du -sh ${TARGET_LIB_ALPINE}
