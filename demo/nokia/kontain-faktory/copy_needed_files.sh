#!/bin/bash
#
# Copy KONTAIN_DIR with files needed for docker build of Kontainerized nokia kafka stuff,
# using pre-build java.km libs in the build tree

set -e ; [ "$TRACE" ] && set -x

if [ -z "${KM_FILES}" ] ; then
   echo KM_FILES is not set; false
fi

TOP=$(git rev-parse --show-toplevel)
if [ -z "${TOP}" ] ; then
   echo Failed to find git toplevel - the script needs to run inside of KM build tree ; false
fi

JDK_VERSION=${1:-jdk-11.0.8+10}
DOCKER_DIR=${2:-.dockerdir}
KONTAIN_DIR=/opt/kontain
jdk_image_dir=${TOP}/payloads/java/${JDK_VERSION}/build/linux-x86_64-normal-server-release/images/jdk

echo Preparing ${DOCKER_DIR}...
if [ ! -d ${DOCKER_DIR} ] ; then mkdir -p ${DOCKER_DIR} ; fi
rm -rf ${DOCKER_DIR}/*

echo Copying ${KONTAIN_DIR}...
tar -C ${KONTAIN_DIR} --exclude='*.a' -cf - . | tar -C ${DOCKER_DIR} -xf -

echo Copying KM files
bin=${DOCKER_DIR}/${JDK_VERSION}/bin
mkdir -p $bin
cp --preserve=all ${KM_FILES} $bin

echo Copying libs from ${jdk_image_dir}
cp -rf --preserve=all ${jdk_image_dir}/lib ${DOCKER_DIR}/${JDK_VERSION}

echo Stripping Kontain and Java .so files... this saves ~600MB of unneeded debug tables
# find and strip all .so* files in lib64 and jdk which are actual ELF files (there are some .py stuff, and symlinks there too)
find ${DOCKER_DIR} -name '*.so*' -size +10k  | grep -v runtime | xargs file -h | awk -F: '/ELF/ {print $1}' | xargs strip

ln -sf /opt/kontain/bin/km ${DOCKER_DIR}/${JDK_VERSION}/bin/java