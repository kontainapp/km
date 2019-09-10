#!/bin/bash
set -ex

if [ ! -d cpython ]; then
    git clone https://github.com/python/cpython.git -b v3.7.4
    pushd cpython
    ./configure CFLAGS="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" LDFLAGS="-static" --disable-shared
    patch < ../pyconfig.h-patch || true
    cp ../Setup.local Modules/Setup.local
    patch -p1 < ../unittest.patch
else
    pushd cpython
fi

make -j16 LDFLAGS="-static" LINKFORSHARED=" " DYNLOADFILE="dynload_stub.o"
popd

./link-km.sh

echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

