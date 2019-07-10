#!/bin/bash

set -x

if [ ! -d node ]; then
    git clone https://github.com/nodejs/node.git -b v12.4.0
    pushd node
    patch -p1 < ../openssl.patch
    ./configure --fully-static --gdb --debug
else
    pushd node
fi

make -j8
popd

./link-km.sh

echo ""
echo "now you can run ``../../build/km/km ./node/out/Release/node.km''"

