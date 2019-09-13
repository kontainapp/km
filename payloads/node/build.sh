#!/bin/bash

set -x

if [ $# -ne 0 ] ; then BUILD=$1 ; else BUILD=Release ; fi
if [ $BUILD == 'Debug' ] ; then CONFIG_DEBUG=--debug ; fi

NODE=node/out/$BUILD/node
KM=`realpath ../../build/km/km`

if [ ! -d node ]; then
    git clone https://github.com/nodejs/node.git -b v12.4.0
    pushd node
    patch -p1 < ../openssl.patch
    ./configure --fully-static --gdb $CONFIG_DEBUG
else
    pushd node
fi

make -j16
popd

./link-km.sh $BUILD
mv $NODE ${NODE}.static

echo "#!$KM --copyenv" > ${NODE}.sh
chmod +x ${NODE}.sh
ln -s node.sh ${NODE}

echo ""
echo "now you can run ``$KM ${NODE}.km''"

