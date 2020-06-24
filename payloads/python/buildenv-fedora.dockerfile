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
# Dockerfile for build python image. There are two stages:
#
# buildenv-cpython - based on kontain/buildenv-km-fedora, git clone, compile and test node
# linkenv - based on km-build-env and just copy the objects and test files

ARG MODE=Release
ARG VERS=v3.7.4
ARG BUILDENV_IMAGE_VERSION=latest

# Form alpine-based container to extract alpine-libs from
# This is a temp stage, so we don't care about layers count.
FROM alpine:3.11.5 as alpine-lib-image
ENV PREFIX=/opt/kontain

RUN apk add sqlite-static

# Prepare $PREFIX/alpine-lib while trying to filter out irrelevant stuff
RUN mkdir -p $PREFIX/alpine-lib
RUN tar cf - -C / lib usr/lib \
   --exclude include\* --exclude finclude --exclude install\* \
   --exclude plugin --exclude pkgconfig --exclude apk \
   --exclude firmware --exclude mdev --exclude bash \
   --exclude engines-\* | tar xf - -C $PREFIX/alpine-lib

FROM kontain/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION} AS buildenv-cpython
ARG VERS

USER root
RUN dnf install libffi-devel xz-devel sqlite-devel expat-static python3-jinja2 -y && dnf clean all && rm -rf /var/cache/{dnf,yum}
USER $USER

RUN git clone https://github.com/python/cpython.git -b $VERS
RUN cd cpython && ./configure && make -j`expr 2 \* $(nproc)` | tee bear.out
COPY platform_uname.patch platform_uname.patch
# Note: this patch also needs to be applied in 'make fromsrc'- see ./Makefile
RUN patch -p0 < platform_uname.patch

WORKDIR /home/$USER/cpython
COPY extensions/ ../extensions/
# prepare "default" .so extensions to be statically linked in, and save neccessary files
RUN ../extensions/prepare_extension.py bear.out --no_mung --log=quiet --skip ../extensions/skip_builtins.txt \
   && files="dlstatic_km.mk build/temp.* `find build -name '*.km.*'` `find build -name '*\.so'`" ; \
   tar cf - $files | (mkdir builtins; tar -C builtins -xf -)

# Build the target image
FROM kontain/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION}
ENV PYTHONTOP=/home/$USER/cpython
#
# The following copies two sets of artifacts - objects needed to build (link) python.km,
# and the sets of files to run set of tests. The former is used by link-km.sh,
# the latter is copied to the output by Makefile using NODE_DISTRO_FILES.
#
ARG BUILD_LOC="/home/$USER/cpython/build/lib.linux-x86_64-3.7/ /home/$USER/cpython/build/temp.linux-x86_64-3.7"

COPY --from=alpine-lib-image $PREFIX $PREFIX/

RUN mkdir -p ${BUILD_LOC} && chown $USER ${BUILD_LOC}
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/builtins cpython/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Lib/ cpython/Lib/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Modules/ cpython/Modules/
COPY --from=buildenv-cpython --chown=appuser:appuser \
   /home/$USER/cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py \
   cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/build/temp.linux-x86_64-3.7 cpython/build/temp.linux-x86_64-3.7/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Programs/python.o cpython/Programs/python.o
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/libpython3.7m.a cpython/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/pybuilddir.txt cpython/
