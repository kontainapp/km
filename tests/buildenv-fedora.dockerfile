# Copyright © 2019-2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
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
FROM alpine:3.13.0 as alpine-lib-image
ENV PREFIX=/opt/kontain

RUN apk add bash make git g++ gcc musl-dev libffi-dev sqlite-static

# Prepare $PREFIX/alpine-lib while trying to filter out irrelevant stuff
RUN mkdir -p $PREFIX/alpine-lib
RUN tar cf - -C / lib usr/lib \
   --exclude include\* --exclude finclude --exclude install\* \
   --exclude plugin --exclude pkgconfig --exclude apk \
   --exclude firmware --exclude mdev --exclude bash \
   --exclude engines-\* | tar xf - -C $PREFIX/alpine-lib
# Alpine 3.12+ bumped libffi version, Fedora 33 hasn't yet. Hack to support that
RUN ln -sf /opt/kontain/alpine-lib/usr/lib/libffi.so.7 /opt/kontain/alpine-lib/usr/lib/libffi.so.6

# Save the path to gcc versioned libs for the future
RUN dirname $(gcc --print-file-name libgcc.a) > $PREFIX/alpine-lib/gcc-libs-path.txt

FROM fedora:31 AS buildenv-early
# Some of the packages needed only for payloads and /or faktory, but we land them here for convenience.
#
# Also, this list is used on generating local build environment, so we explicitly add
# some packages which are always present on Fedora32 but may be missing on Fedora31 (e.g. python3-markupsafe).
# We have also added packages needed by python.km extensions json generation (jq, googler) and crun's build:
#   automake autoconf libcap-devel yajl-devel libseccomp-devel
#   python3-libmount libtool
RUN dnf install -y \
   gcc gcc-c++ make gdb git-core gcovr \
   time patch file findutils diffutils which procps-ng python2 \
   glibc-devel glibc-static libstdc++-static \
   elfutils-libelf-devel bzip2-devel \
   zlib-static bzip2-static xz-static \
   openssl-devel openssl-static jq googler \
   python3-markupsafe parallel \
   automake autoconf libcap-devel yajl-devel libseccomp-devel \
   python3-libmount libtool \
   && dnf upgrade -y && dnf clean all && rm -rf /var/cache/{dnf,yum}

FROM buildenv-early AS buildlibelf

RUN dnf install -y flex bison zstd gettext-devel bsdtar xz-devel
RUN git clone git://sourceware.org/git/elfutils.git -b elfutils-0.182 && cd elfutils && \
   autoreconf -i -f && \
   ./configure --enable-maintainer-mode --disable-libdebuginfod --disable-debuginfod && make -j && make install

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

COPY --from=buildlibelf /usr/local /usr/local/
COPY --from=alpine-lib-image $PREFIX $PREFIX/

USER $USER
