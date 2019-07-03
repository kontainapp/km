#!/bin/bash

set -x

KM=../..
NODE=node

g++ -o $NODE/out/Release/node.km \
  -static -no-pie -ggdb \
  -specs=$KM/gcc-km.spec -L $KM/build/runtime \
  -Wl,--start-group \
    $NODE/out/Release/obj.target/node/src/node_main.o \
    $NODE/out/Release/obj.target/node/src/node_snapshot_stub.o \
    $NODE/out/Release/obj.target/node/gen/node_code_cache.o \
    $NODE/out/Release/obj.target/deps/histogram/libhistogram.a \
    $NODE/out/Release/obj.target/libnode.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
    $NODE/out/Release/obj.target/tools/icu/libicui18n.a \
    $NODE/out/Release/obj.target/deps/zlib/libzlib.a \
    $NODE/out/Release/obj.target/deps/http_parser/libhttp_parser.a \
    $NODE/out/Release/obj.target/deps/llhttp/libllhttp.a \
    $NODE/out/Release/obj.target/deps/cares/libcares.a \
    $NODE/out/Release/obj.target/deps/uv/libuv.a \
    $NODE/out/Release/obj.target/deps/nghttp2/libnghttp2.a \
    $NODE/out/Release/obj.target/deps/brotli/libbrotli.a \
    $NODE/out/Release/obj.target/deps/openssl/libopenssl.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_base.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libbase.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libsampler.a \
    $NODE/out/Release/obj.target/tools/icu/libicuucx.a \
    $NODE/out/Release/obj.target/tools/icu/libicudata.a \
    $NODE/out/Release/obj.target/tools/icu/libicustubdata.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
    $NODE/out/Release/obj.target/tools/v8_gypfiles/libgenerate_snapshot.a \
  -Wl,--end-group
