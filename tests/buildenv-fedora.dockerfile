# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile for build image. There are three stages:
#
# buildenv-base - basic from OS image with the necessary packages installed
# build-libstdcpp - clone gcc source, then build and install libstdc++
# buildenv - based on buildenv-base and just copy the installed part from the previous
#
# Usage will be 'docker run <container> make TARGET=<target> - see ../../Makefile

FROM fedora:31 AS buildenv-base
ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
ARG UID=1001
ARG GID=117

RUN groupadd -f -g $GID $USER && useradd -r -u $UID -g $GID $USER && mkdir /home/$USER && chown $UID.$GID /home/$USER

ENV TERM=xterm
ENV USER=$USER
ENV PREFIX=/opt/kontain
WORKDIR /home/$USER

RUN dnf install -y \
   gcc gcc-c++ make gdb git gcovr \
   time patch file findutils diffutils moreutils which procps-ng python2 \
   glibc-devel glibc-static libstdc++-static clang \
   elfutils-libelf-devel elfutils-libelf-devel-static  bzip2-devel \
   zlib-static bzip2-static xz-static \
   openssl-devel openssl-static \
   expat-static \
   && dnf upgrade -y && dnf clean all

FROM buildenv-base AS buildenv-gcc-base
RUN dnf install -y gmp-devel mpfr-devel libmpc-devel isl-devel flex m4 \
   autoconf automake libtool texinfo

FROM buildenv-gcc-base AS build-libstdcpp
ARG LIBSTDCPPVER=gcc-9_2_0-kontain
USER $USER

RUN git clone https://github.com/kontainapp/gcc.git -b $LIBSTDCPPVER
RUN mkdir -p build_gcc && cd build_gcc \
   && ../gcc/configure --prefix=$PREFIX --enable-clocale=generic --disable-bootstrap --enable-languages=c,c++ \
   --enable-threads=posix --enable-checking=release --disable-multilib --with-system-zlib --enable-__cxa_atexit \
   --disable-libunwind-exceptions --enable-gnu-unique-object --enable-linker-build-id --with-gcc-major-version-only \
   --with-linker-hash-style=gnu --enable-plugin --enable-initfini-array --with-isl --without-cuda-driver \
   --enable-gnu-indirect-function --enable-cet --with-tune=generic \
   && make -j`expr 2 \* $(nproc)` && cd x86_64-pc-linux-gnu/libstdc++-v3 && make clean \
   && sed -i -e 's/^#define *HAVE___CXA_THREAD_ATEXIT_IMPL.*$/\/* & *\//' config.h \
   && make -j`expr 2 \* $(nproc)`

RUN git clone https://github.com/libffi/libffi -b v3.2.1
RUN cd libffi && ./autogen.sh && ./configure --prefix=$PREFIX && make -j

USER root
RUN mkdir -p $PREFIX && make -C build_gcc/x86_64-pc-linux-gnu/libstdc++-v3 install && make -C build_gcc/x86_64-pc-linux-gnu/libgcc install
RUN make -C libffi install

FROM buildenv-base AS buildenv
LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"
USER $USER

COPY --from=build-libstdcpp $PREFIX/ $PREFIX
