#!/bin/bash

# Create node.km by linking the objects files from build artifacts located at $1 (like /home/appuser/node in a "blank")
# and put the result into location at $2

set -x

if [[ $# -ne 0 ]] ; then NODE=$1 ; else exit 1 ; fi
OUT=${2:-$NODE}
PATH=../../tools:$PATH

link_node() {
   kontain-g++ -ggdb -o $OUT/node.km \
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

link_cctest() {
   kontain-g++ -ggdb -o $OUT/cctest.km \
   -Wl,--start-group \
      $NODE/obj.target/cctest/src/node_snapshot_stub.o \
      $NODE/obj.target/cctest/src/node_code_cache_stub.o \
      $NODE/obj.target/cctest/test/cctest/gtest/gtest-all.o \
      $NODE/obj.target/cctest/test/cctest/gtest/gtest_main.o \
      $NODE/obj.target/cctest/test/cctest/node_test_fixture.o \
      $NODE/obj.target/cctest/test/cctest/test_aliased_buffer.o \
      $NODE/obj.target/cctest/test/cctest/test_base64.o \
      $NODE/obj.target/cctest/test/cctest/test_node_postmortem_metadata.o \
      $NODE/obj.target/cctest/test/cctest/test_environment.o \
      $NODE/obj.target/cctest/test/cctest/test_linked_binding.o \
      $NODE/obj.target/cctest/test/cctest/test_per_process.o \
      $NODE/obj.target/cctest/test/cctest/test_platform.o \
      $NODE/obj.target/cctest/test/cctest/test_report_util.o \
      $NODE/obj.target/cctest/test/cctest/test_traced_value.o \
      $NODE/obj.target/cctest/test/cctest/test_util.o \
      $NODE/obj.target/cctest/test/cctest/test_url.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket_server.o \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/deps/histogram/libhistogram.a \
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

link_node
link_cctest
