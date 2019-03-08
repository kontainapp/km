#!/bin/bash

set -x

# git clone git@bitbucket.org:kontainapp/covm.git km
# cd km
# git checkout serge/python
# git submodule update --init

pushd ../../runtime/musl
./configure --disable-shared
make -j8
popd

if [ ! -d cpython ]; then
    git clone https://github.com/python/cpython.git -b 3.7
    pushd cpython
    ./configure --disable-shared
    patch < ../pyconfig.h-patch
    patch < ../Makefile-patch
else
    pushd cpython
fi

make -j8
popd

./link-static-musl.sh
./link-km.sh

echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

