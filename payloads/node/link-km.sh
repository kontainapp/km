#!/bin/bash

set -x

KM=../..
PATH=${KM}/tools:$PATH

link_node() {
   NODE=node/out/$1
   kontain-g++ -o $NODE/node.km \
   -ggdb -z undefs \
   -Wl,--start-group \
      $NODE/obj.target/node/src/node_main.o \
      $NODE/obj.target/node/src/node_snapshot_stub.o \
      $NODE/obj.target/node/gen/node_code_cache.o \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/http_parser/libhttp_parser.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libsampler.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/icu/libicustubdata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libgenerate_snapshot.a \
   -Wl,--end-group
}

link_node Release
link_node Debug