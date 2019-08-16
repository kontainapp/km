#!/bin/bash

set -x

if [ $# -ne 0 ] ; then BUILD=$1 ; else BUILD=Release ; fi
if [ $BUILD == 'Debug' ] ; then CONFIG_DEBUG=--debug ; fi

if [ ! -d node ]; then
    git clone https://github.com/nodejs/node.git -b v12.4.0
    pushd node
    patch -p1 < ../openssl.patch
    ./configure --fully-static --gdb $CONFIG_DEBUG
else
    pushd node
fi

make -j8
popd

./link-km.sh $BUILD

echo ""
echo "now you can run ``../../build/km/km ./node/out/${BUILD}/node.km''"

