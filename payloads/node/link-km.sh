#!/bin/bash -x
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
#
# Create node.km by linking the objects files from build artifacts located at $1 (like /home/appuser/node in a "blank")
# and put the result into location at $2
set -e ; [ "$TRACE" ] && set -x

KM_TOP=$(git rev-parse --show-toplevel)

[[ $# -lt 3 ]] && echo "Usage: ./link-km.sh build-dir out-dir fedoraXX"

NODE=$1
OUT=$2
OS_VERSION=$3

link_node_fedora_40() {
   kontain-g++ -ggdb -o $OUT/node.km \
	   -pthread -rdynamic $NODE/obj.target/node_text_start/src/large_pages/node_text_start.o \
	   -Wl,--whole-archive $NODE/obj.target/libnode.a $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
           -Wl,--no-whole-archive \
           -Wl,--whole-archive $NODE/obj.target/deps/zlib/libzlib.a \
           -Wl,--no-whole-archive \
           -Wl,--whole-archive $NODE/obj.target/deps/uv/libuv.a \
           -Wl,--no-whole-archive \
           -Wl,-z,noexecstack \
           -Wl,--whole-archive $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
           -Wl,--no-whole-archive \
           -Wl,-z,relro \
           -Wl,-z,now \
           -Wl,--whole-archive,$NODE/obj.target/deps/openssl/libopenssl.a \
           -Wl,--no-whole-archive \
           -pthread \
           -m64 \
   -Wl,--start-group \
      $NODE/obj.target/node/src/node_main.o \
      $NODE/obj.target/node/gen/node_snapshot.o \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/libnode_text_start.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/deps/base64/libbase64.a \
      $NODE/obj.target/deps/base64/libbase64_ssse3.a \
      $NODE/obj.target/deps/base64/libbase64_sse41.a \
      $NODE/obj.target/deps/base64/libbase64_sse42.a \
      $NODE/obj.target/deps/base64/libbase64_avx.a \
      $NODE/obj.target/deps/base64/libbase64_avx2.a \
      $NODE/obj.target/deps/base64/libbase64_avx512.a \
      $NODE/obj.target/deps/simdutf/libsimdutf.a \
      $NODE/obj.target/deps/ada/libada.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_turboshaft.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
      $NODE/obj.target/deps/zlib/libzlib_inflate_chunk_simd.a \
      $NODE/obj.target/deps/zlib/libzlib_adler32_simd.a \
      -lm \
      -ldl \
      -L ${KM_TOP}/build/opt/kontain/lib -lmimalloc \
   -Wl,--end-group -pthread
}

link_cctest_fedora_40() {
   kontain-g++ -ggdb -o $OUT/cctest.km \
      -pthread \
      -rdynamic \
      -Wl,--whole-archive $NODE/obj.target/deps/zlib/libzlib.a \
      -Wl,--no-whole-archive \
      -Wl,--whole-archive $NODE/obj.target/deps/uv/libuv.a \
      -Wl,--no-whole-archive \
      -Wl,-z,noexecstack \
      -Wl,--whole-archive $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      -Wl,--no-whole-archive \
      -Wl,-z,relro \
      -Wl,-z,now \
      -Wl,--whole-archive,$NODE/obj.target/deps/openssl/libopenssl.a \
      -Wl,--no-whole-archive \
      -pthread -m64 \
    -Wl,--start-group \
      $NODE/obj.target/cctest/src/node_snapshot_stub.o \
      $NODE/obj.target/cctest/test/cctest/node_test_fixture.o \
      $NODE/obj.target/cctest/test/cctest/test_aliased_buffer.o \
      $NODE/obj.target/cctest/test/cctest/test_base64.o \
      $NODE/obj.target/cctest/test/cctest/test_base_object_ptr.o \
      $NODE/obj.target/cctest/test/cctest/test_cppgc.o \
      $NODE/obj.target/cctest/test/cctest/test_node_postmortem_metadata.o \
      $NODE/obj.target/cctest/test/cctest/test_environment.o \
      $NODE/obj.target/cctest/test/cctest/test_linked_binding.o \
      $NODE/obj.target/cctest/test/cctest/test_node_api.o \
      $NODE/obj.target/cctest/test/cctest/test_per_process.o \
      $NODE/obj.target/cctest/test/cctest/test_platform.o \
      $NODE/obj.target/cctest/test/cctest/test_report.o \
      $NODE/obj.target/cctest/test/cctest/test_json_utils.o \
      $NODE/obj.target/cctest/test/cctest/test_sockaddr.o \
      $NODE/obj.target/cctest/test/cctest/test_traced_value.o \
      $NODE/obj.target/cctest/test/cctest/test_util.o \
      $NODE/obj.target/cctest/test/cctest/test_dataqueue.o \
      $NODE/obj.target/cctest/test/cctest/test_crypto_clienthello.o \
      $NODE/obj.target/cctest/test/cctest/test_node_crypto.o \
      $NODE/obj.target/cctest/test/cctest/test_node_crypto_env.o \
      $NODE/obj.target/cctest/test/cctest/test_quic_cid.o \
      $NODE/obj.target/cctest/test/cctest/test_quic_tokens.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket_server.o \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/deps/base64/libbase64.a \
      $NODE/obj.target/deps/googletest/libgtest.a \
      $NODE/obj.target/deps/googletest/libgtest_main.a \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/deps/simdutf/libsimdutf.a \
      $NODE/obj.target/deps/ada/libada.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/deps/base64/libbase64_ssse3.a \
      $NODE/obj.target/deps/base64/libbase64_sse41.a \
      $NODE/obj.target/deps/base64/libbase64_sse42.a \
      $NODE/obj.target/deps/base64/libbase64_avx.a \
      $NODE/obj.target/deps/base64/libbase64_avx2.a \
      $NODE/obj.target/deps/base64/libbase64_avx512.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_turboshaft.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
      $NODE/obj.target/deps/zlib/libzlib_inflate_chunk_simd.a \
      $NODE/obj.target/deps/zlib/libzlib_adler32_simd.a \
      -lm -ldl \
    -Wl,--end-group
}

link_node_fedora_39() {
   kontain-g++ -ggdb -o $OUT/node.km \
	   -pthread -rdynamic $NODE/obj.target/node_text_start/src/large_pages/node_text_start.o \
	   -Wl,--whole-archive $NODE/obj.target/libnode.a $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
           -Wl,--no-whole-archive \
           -Wl,--whole-archive $NODE/obj.target/deps/zlib/libzlib.a \
           -Wl,--no-whole-archive \
           -Wl,--whole-archive $NODE/obj.target/deps/uv/libuv.a \
           -Wl,--no-whole-archive \
           -Wl,-z,noexecstack \
           -Wl,--whole-archive $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
           -Wl,--no-whole-archive \
           -Wl,-z,relro \
           -Wl,-z,now \
           -Wl,--whole-archive,$NODE/obj.target/deps/openssl/libopenssl.a \
           -Wl,--no-whole-archive \
           -pthread \
           -m64 \
   -Wl,--start-group \
      $NODE/obj.target/node/src/node_main.o \
      $NODE/obj.target/node/gen/node_snapshot.o \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/libnode_text_start.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/deps/base64/libbase64.a \
      $NODE/obj.target/deps/base64/libbase64_ssse3.a \
      $NODE/obj.target/deps/base64/libbase64_sse41.a \
      $NODE/obj.target/deps/base64/libbase64_sse42.a \
      $NODE/obj.target/deps/base64/libbase64_avx.a \
      $NODE/obj.target/deps/base64/libbase64_avx2.a \
      $NODE/obj.target/deps/simdutf/libsimdutf.a \
      $NODE/obj.target/deps/ada/libada.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_turboshaft.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
      $NODE/obj.target/deps/zlib/libzlib_inflate_chunk_simd.a \
      $NODE/obj.target/deps/zlib/libzlib_adler32_simd.a \
      -lm \
      -ldl \
      -L ${KM_TOP}/build/opt/kontain/lib -lmimalloc \
   -Wl,--end-group -pthread
}

link_cctest_fedora_39() {
   kontain-g++ -ggdb -o $OUT/cctest.km \
      -pthread \
      -rdynamic \
      -Wl,--whole-archive $NODE/obj.target/deps/zlib/libzlib.a \
      -Wl,--no-whole-archive \
      -Wl,--whole-archive $NODE/obj.target/deps/uv/libuv.a \
      -Wl,--no-whole-archive \
      -Wl,-z,noexecstack \
      -Wl,--whole-archive $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      -Wl,--no-whole-archive \
      -Wl,-z,relro \
      -Wl,-z,now \
      -Wl,--whole-archive,$NODE/obj.target/deps/openssl/libopenssl.a \
      -Wl,--no-whole-archive \
      -pthread -m64 \
    -Wl,--start-group \
      $NODE/obj.target/cctest/src/node_snapshot_stub.o \
      $NODE/obj.target/cctest/test/cctest/node_test_fixture.o \
      $NODE/obj.target/cctest/test/cctest/test_aliased_buffer.o \
      $NODE/obj.target/cctest/test/cctest/test_base64.o \
      $NODE/obj.target/cctest/test/cctest/test_base_object_ptr.o \
      $NODE/obj.target/cctest/test/cctest/test_cppgc.o \
      $NODE/obj.target/cctest/test/cctest/test_node_postmortem_metadata.o \
      $NODE/obj.target/cctest/test/cctest/test_environment.o \
      $NODE/obj.target/cctest/test/cctest/test_linked_binding.o \
      $NODE/obj.target/cctest/test/cctest/test_node_api.o \
      $NODE/obj.target/cctest/test/cctest/test_per_process.o \
      $NODE/obj.target/cctest/test/cctest/test_platform.o \
      $NODE/obj.target/cctest/test/cctest/test_report.o \
      $NODE/obj.target/cctest/test/cctest/test_json_utils.o \
      $NODE/obj.target/cctest/test/cctest/test_sockaddr.o \
      $NODE/obj.target/cctest/test/cctest/test_traced_value.o \
      $NODE/obj.target/cctest/test/cctest/test_util.o \
      $NODE/obj.target/cctest/test/cctest/test_dataqueue.o \
      $NODE/obj.target/cctest/test/cctest/test_crypto_clienthello.o \
      $NODE/obj.target/cctest/test/cctest/test_node_crypto.o \
      $NODE/obj.target/cctest/test/cctest/test_node_crypto_env.o \
      $NODE/obj.target/cctest/test/cctest/test_quic_cid.o \
      $NODE/obj.target/cctest/test/cctest/test_quic_tokens.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket_server.o \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/deps/base64/libbase64.a \
      $NODE/obj.target/deps/googletest/libgtest.a \
      $NODE/obj.target/deps/googletest/libgtest_main.a \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/deps/simdutf/libsimdutf.a \
      $NODE/obj.target/deps/ada/libada.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/deps/base64/libbase64_ssse3.a \
      $NODE/obj.target/deps/base64/libbase64_sse41.a \
      $NODE/obj.target/deps/base64/libbase64_sse42.a \
      $NODE/obj.target/deps/base64/libbase64_avx.a \
      $NODE/obj.target/deps/base64/libbase64_avx2.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_turboshaft.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
      $NODE/obj.target/deps/zlib/libzlib_inflate_chunk_simd.a \
      $NODE/obj.target/deps/zlib/libzlib_adler32_simd.a \
      -lm -ldl \
    -Wl,--end-group
}

link_node() {
   kontain-g++ -ggdb -o $OUT/node.km \
   -Wl,--start-group \
      $NODE/obj.target/node_text_start/src/large_pages/node_text_start.o \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/node/src/node_main.o \
      $NODE/obj.target/node/gen/node_code_cache.o \
      $NODE/obj.target/node/gen/node_snapshot.o \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/libnode_text_start.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
      -L ${KM_TOP}/build/opt/kontain/lib -lmimalloc \
   -Wl,--end-group -pthread
}

link_cctest() {
   kontain-g++ -ggdb -o $OUT/cctest.km \
   -Wl,--start-group -Wl,--no-whole-archive \
      $NODE/obj.target/cctest/src/node_snapshot_stub.o \
      $NODE/obj.target/cctest/src/node_code_cache_stub.o \
      $NODE/obj.target/cctest/test/cctest/node_test_fixture.o \
      $NODE/obj.target/cctest/test/cctest/test_aliased_buffer.o \
      $NODE/obj.target/cctest/test/cctest/test_base64.o \
      $NODE/obj.target/cctest/test/cctest/test_base_object_ptr.o \
      $NODE/obj.target/cctest/test/cctest/test_node_postmortem_metadata.o \
      $NODE/obj.target/cctest/test/cctest/test_environment.o \
      $NODE/obj.target/cctest/test/cctest/test_js_native_api_v8.o \
      $NODE/obj.target/cctest/test/cctest/test_linked_binding.o \
      $NODE/obj.target/cctest/test/cctest/test_node_api.o \
      $NODE/obj.target/cctest/test/cctest/test_per_process.o \
      $NODE/obj.target/cctest/test/cctest/test_platform.o \
      $NODE/obj.target/cctest/test/cctest/test_json_utils.o \
      $NODE/obj.target/cctest/test/cctest/test_sockaddr.o \
      $NODE/obj.target/cctest/test/cctest/test_traced_value.o \
      $NODE/obj.target/cctest/test/cctest/test_util.o \
      $NODE/obj.target/cctest/test/cctest/test_url.o \
      $NODE/obj.target/cctest/test/cctest/test_node_crypto.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket.o \
      $NODE/obj.target/cctest/test/cctest/test_inspector_socket_server.o \
      $NODE/obj.target/libnode.a \
      $NODE/obj.target/deps/googletest/libgtest.a \
      $NODE/obj.target/deps/googletest/libgtest_main.a \
      $NODE/obj.target/deps/histogram/libhistogram.a \
      $NODE/obj.target/deps/uvwasi/libuvwasi.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
      $NODE/obj.target/tools/icu/libicui18n.a \
      $NODE/obj.target/deps/zlib/libzlib.a \
      $NODE/obj.target/deps/llhttp/libllhttp.a \
      $NODE/obj.target/deps/cares/libcares.a \
      $NODE/obj.target/deps/uv/libuv.a \
      $NODE/obj.target/deps/nghttp2/libnghttp2.a \
      $NODE/obj.target/deps/brotli/libbrotli.a \
      $NODE/obj.target/deps/openssl/libopenssl.a \
      $NODE/obj.target/deps/ngtcp2/libngtcp2.a \
      $NODE/obj.target/deps/ngtcp2/libnghttp3.a \
      $NODE/obj.target/tools/icu/libicuucx.a \
      $NODE/obj.target/tools/icu/libicudata.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_base_without_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_libbase.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_zlib.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_compiler.a \
      $NODE/obj.target/tools/v8_gypfiles/libv8_initializers.a \
   -Wl,--end-group -pthread
}

if [[ "${OS_VERSION}" == "fedora40" ]] ; then
	link_node_fedora_40
	link_cctest_fedora_40
elif [[ "${OS_VERSION}" == "fedora39" ]] ; then
	link_node_fedora_39
	link_cctest_fedora_39
else
	link_node
	link_cctest
fi
