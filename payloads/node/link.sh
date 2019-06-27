#!/bin/bash

KM=../..
NODE=node

# g++ -o $NODE/out/Release/node.km \
#   -pthread \
#   -Wl,--start-group \
#     $NODE/out/Release/obj.target/node/src/node_main.o \
#     $NODE/out/Release/obj.target/node/src/node_snapshot_stub.o \
#     $NODE/out/Release/obj.target/node/gen/node_code_cache.o \
#     $NODE/out/Release/obj.target/deps/histogram/libhistogram.a \
#     $NODE/out/Release/obj.target/libnode.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
#     $NODE/out/Release/obj.target/tools/icu/libicui18n.a \
#     $NODE/out/Release/obj.target/deps/zlib/libzlib.a \
#     $NODE/out/Release/obj.target/deps/http_parser/libhttp_parser.a \
#     $NODE/out/Release/obj.target/deps/llhttp/libllhttp.a \
#     $NODE/out/Release/obj.target/deps/cares/libcares.a \
#     $NODE/out/Release/obj.target/deps/uv/libuv.a \
#     $NODE/out/Release/obj.target/deps/nghttp2/libnghttp2.a \
#     $NODE/out/Release/obj.target/deps/brotli/libbrotli.a \
#     $NODE/out/Release/obj.target/deps/openssl/libopenssl.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_base.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libbase.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libsampler.a \
#     $NODE/out/Release/obj.target/tools/icu/libicuucx.a \
#     $NODE/out/Release/obj.target/tools/icu/libicudata.a \
#     $NODE/out/Release/obj.target/tools/icu/libicustubdata.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libgenerate_snapshot.a 
#     -static -ldl -lrt -lm \
#   -Wl,--end-group

# g++ -v -o $NODE/out/Release/node.km \
#   -static -no-pie -ggdb -pthread \
#   -specs=$KM/gcc-km.spec -L /home/serge/workspace/km/build/runtime \
#   -Wl,--start-group \
#     $NODE/out/Release/obj.target/node/src/node_main.o \
#     $NODE/out/Release/obj.target/node/src/node_snapshot_stub.o \
#     $NODE/out/Release/obj.target/node/gen/node_code_cache.o \
#     $NODE/out/Release/obj.target/deps/histogram/libhistogram.a \
#     $NODE/out/Release/obj.target/libnode.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libplatform.a \
#     $NODE/out/Release/obj.target/tools/icu/libicui18n.a \
#     $NODE/out/Release/obj.target/deps/zlib/libzlib.a \
#     $NODE/out/Release/obj.target/deps/http_parser/libhttp_parser.a \
#     $NODE/out/Release/obj.target/deps/llhttp/libllhttp.a \
#     $NODE/out/Release/obj.target/deps/cares/libcares.a \
#     $NODE/out/Release/obj.target/deps/uv/libuv.a \
#     $NODE/out/Release/obj.target/deps/nghttp2/libnghttp2.a \
#     $NODE/out/Release/obj.target/deps/brotli/libbrotli.a \
#     $NODE/out/Release/obj.target/deps/openssl/libopenssl.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_base.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libbase.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_libsampler.a \
#     $NODE/out/Release/obj.target/tools/icu/libicuucx.a \
#     $NODE/out/Release/obj.target/tools/icu/libicudata.a \
#     $NODE/out/Release/obj.target/tools/icu/libicustubdata.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libv8_snapshot.a \
#     $NODE/out/Release/obj.target/tools/v8_gypfiles/libgenerate_snapshot.a \
#   -Wl,--end-group

/usr/libexec/gcc/x86_64-redhat-linux/9/collect2 -plugin /usr/libexec/gcc/x86_64-redhat-linux/9/liblto_plugin.so -plugin-opt=/usr/libexec/gcc/x86_64-redhat-linux/9/lto-wrapper -plugin-opt=-fresolution=/tmp/ccVL6JK9.res -plugin-opt=-pass-through=-lgcc \
  -plugin-opt=-pass-through=-lgcc_eh -plugin-opt=-pass-through=-lruntime -plugin-opt=-pass-through=-lpthread --hash-style=gnu -m elf_x86_64 \
  -static -Ttext-segment=0x1FF000 -u__km_handle_interrupt -u__km_handle_signal -e__start_c__ --gc-sections -zseparate-code -znorelro -zmax-page-size=0x1000 -zundefs --build-id=none \
  -o $NODE/out/Release/node.km \
  /usr/lib/gcc/x86_64-redhat-linux/9/crtbeginT.o -L$KM/build/runtime -L/usr/lib/gcc/x86_64-redhat-linux/9 -L/usr/lib/gcc/x86_64-redhat-linux/9/../../../../lib64 -L/lib/../lib64 -L/usr/lib/../lib64 -L/usr/lib/gcc/x86_64-redhat-linux/9/../../.. \
  --start-group \
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
  --end-group \
  -lstdc++ --start-group -lgcc -lgcc_eh -lruntime -lkmpthread --end-group /usr/lib/gcc/x86_64-redhat-linux/9/crtend.o
