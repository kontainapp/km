#!/bin/bash
set -ex

if [ ! -d cpython ]; then
    git clone https://github.com/python/cpython.git -b v3.7.4
    pushd cpython
    ./configure
    patch -p1 < ../unittest.patch
    patch -p0 < ../makesetup.patch
else
    pushd cpython
fi

cp ../Setup.local Modules/Setup.local
make -j`expr 2 \* $(nproc)`
popd

./link-km.sh

echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

