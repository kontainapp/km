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

# list cpython unittest we do not want to run.
TEST_PATH="./Lib/unittest/test"
TEST_NAMES=(\
  test_case.py \
  test_discovery.py \
  test_program.py \
  test_result.py \
  test_runner.py \
)

if [ ! -d cpython ]; then
    git clone https://github.com/python/cpython.git -b v3.7.1
    pushd cpython
    set -e
    ./configure LDFLAGS="-static" --disable-shared
    patch < ../pyconfig.h-patch
    cp ../Setup.local Modules/Setup.local
    # Prefix tests differently to ensure that they are not run.
    for tname in "${TEST_NAMES[@]}"; do
        mv -v $TEST_PATH/$tname $TEST_PATH/__km_disable__$tname
    done
    set +e
else
    pushd cpython
fi

make -j8 LDFLAGS="-static" LINKFORSHARED=" " DYNLOADFILE="dynload_stub.o"
popd

./link-static-musl.sh
./link-km.sh

echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

