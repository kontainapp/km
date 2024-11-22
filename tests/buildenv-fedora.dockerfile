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
# Dockerfile for buildenv image - these are the base images for KM, tests and payload builds.
#
# There are three stages:
#
# alpine-lib-image - build image with alpine libs we need for runtime
# buildenv - fedora + DNF packages we need, and alpine packages for runtime
#
# Usage: 'docker run <container> make TARGET=<target>

# Form alpine-based container to extract alpine-libs from
# This is a temp stage, so we don't care about layers count.
FROM alpine:3.20.0 AS alpine-lib-image
ENV PREFIX=/opt/kontain
ARG LIBSTDCPPVER=releases/gcc-13.2.0

RUN apk add bash file make git g++ gcc flex bison musl-dev libffi-dev sqlite-static util-linux-dev gmp-dev mpfr-dev mpc1-dev isl-dev zlib-dev

USER $USER

RUN git clone git://gcc.gnu.org/git/gcc.git -b $LIBSTDCPPVER
RUN mkdir -p build_gcc && cd build_gcc \
   && ../gcc/configure --prefix=/usr/local --enable-clocale=generic --disable-bootstrap --enable-languages=c,c++ \
   --enable-threads=posix --enable-checking=release --disable-multilib --with-system-zlib --enable-__cxa_atexit \
   --disable-libunwind-exceptions --enable-gnu-unique-object --enable-linker-build-id --with-gcc-major-version-only \
   --with-linker-hash-style=gnu --enable-plugin --enable-initfini-array --with-isl --without-cuda-driver \
   --enable-gnu-indirect-function --enable-cet --with-tune=generic --disable-libsanitizer \
   && make -j`expr 2 \* $(nproc)` && cd x86_64-pc-linux-musl/libstdc++-v3 && make clean \
   && sed -i -e 's/^#define *HAVE___CXA_THREAD_ATEXIT_IMPL.*$/\/* & *\//' config.h \
   && make -j`expr 2 \* $(nproc)`

USER root
RUN make -C build_gcc/x86_64-pc-linux-musl/libstdc++-v3 install

# Prepare $PREFIX/{runtime,alpine-lib} while trying to filter out irrelevant stuff
RUN mkdir -p $PREFIX/alpine-lib && mkdir -p $PREFIX/runtime
RUN tar cf - -C / lib usr/lib \
   --exclude include\* --exclude finclude --exclude install\* \
   --exclude plugin --exclude pkgconfig --exclude apk \
   --exclude firmware --exclude mdev --exclude bash \
   --exclude engines-\* | tar xf - -C $PREFIX/alpine-lib
# Alpine 3.12+ bumped libffi version, Fedora 33 hasn't yet. Hack to support that
RUN ln -sf libffi.so.7 ${PREFIX}/alpine-lib/usr/lib/libffi.so.6
RUN tar -cf - -C /usr/local/lib64 . | tar xf - -C $PREFIX/runtime

# Save the path to gcc versioned libs for the future
RUN dirname $(gcc --print-file-name libgcc.a) > $PREFIX/alpine-lib/gcc-libs-path.txt

FROM fedora:39 AS buildenv-early
# Some of the packages needed only for payloads and /or faktory, but we land them here for convenience.
#
# Also, this list is used on generating local build environment, so we explicitly add
# some packages which are always present on Fedora32 but may be missing on Fedora31 (e.g. python3-markupsafe)
# We have also added packages needed by python.km extensions json generation (jq, googler), by python configuration (libffi-devel) and crun's build:
#   automake autoconf libcap-devel yajl-devel libseccomp-devel
#   python3-libmount libtool
RUN dnf install -y 'dnf-command(config-manager)' \
   && dnf config-manager --add-repo https://cli.github.com/packages/rpm/gh-cli.repo
RUN dnf install -y \
   gcc gcc-c++ make gdb git-core gcovr gh \
   time patch file findutils diffutils which procps-ng python2 \
   glibc-devel glibc-static libstdc++-static bzip2-devel \
   golang-sigs-k8s-kustomize \
   zlib-static bzip2-static xz-static xz \
   openssl-devel jq googler \
   python3-markupsafe libffi-devel parallel \
   automake autoconf libcap-devel yajl-devel libseccomp-devel \
   python3-libmount libtool cmake makeself \
   systemd-devel valgrind lsof \
   && dnf upgrade -y && dnf clean all && rm -rf /var/cache/{dnf,yum}

FROM buildenv-early AS buildclangformat

RUN git clone https://github.com/llvm/llvm-project.git -b llvmorg-17.0.0 && cd llvm-project && \
   cmake -S llvm -B build -G "Unix Makefiles" -DLLVM_ENABLE_PROJECTS="clang" -DCMAKE_BUILD_TYPE=Release && \
   make -C build/tools/clang/tools/clang-format -j`expr 2 \* $(nproc)` && \
   make -C build/tools/clang/tools/clang-format install

RUN pwd
RUN git clone https://github.com/kontainapp/musl-latest.git -b alpine musl-latest-alpine && cd musl-latest-alpine && \
   ./configure --enable-debug CFLAGS=-D_LARGEFILE64_SOURCE && make
RUN ls /musl-latest-alpine/lib/libc.*

FROM buildenv-early AS buildenv
ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
ARG UID=1001
ARG GID=117

RUN groupadd -f -g $GID $USER && useradd -r -u $UID -g $GID $USER && mkdir /home/$USER && chown $UID.$GID /home/$USER

ENV TERM=xterm
ENV USER=$USER
ENV PREFIX=/opt/kontain
WORKDIR /home/$USER

COPY --from=buildclangformat /usr/local/bin /usr/local/bin/
COPY --from=alpine-lib-image $PREFIX $PREFIX/
COPY --from=buildclangformat /musl-latest-alpine/lib/libc.a ${PREFIX}/alpine-lib/usr/lib/libc.a
COPY --from=buildclangformat /musl-latest-alpine/lib/libc.so ${PREFIX}/alpine-lib/usr/lib/libc.so

RUN for i in runtime alpine-lib ; do \
       mkdir -p $PREFIX/$i && chgrp users $PREFIX/$i && chmod 777 $PREFIX/$i ; \
    done
# take libcrypto from Fedora - python ssl doesn't like alpine one
RUN cp /usr/lib64/libcrypto.so.3.1.1 ${PREFIX}/alpine-lib/lib/

USER $USER
