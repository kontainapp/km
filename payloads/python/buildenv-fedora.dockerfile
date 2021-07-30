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
# Dockerfile for build python image. There are two stages:
#
# buildenv-cpython - based on kontainapp/buildenv-km-fedora, git clone, compile and test node
# linkenv - based on km-build-env and just copy the objects and test files

ARG MODE=Release
# Git tag to fetch. Needs to be passed via build args. E.g. v3.8.3
ARG TAG
# Version to use in paths. Needs to be passed via args. E.g. 3.8
ARG VERS
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION} AS buildenv-cpython
ARG TAG
# ARG VERS

USER root
RUN dnf install libffi-devel xz-devel sqlite-devel expat-static python3-jinja2 -y && dnf clean all && rm -rf /var/cache/{dnf,yum}
USER $USER

RUN git clone https://github.com/python/cpython.git -b $TAG
COPY km_api.c cpython/Modules/km_api.c
COPY 00102-lib64.patch /tmp/
RUN echo "kontain km_api.c" >> cpython/Modules/Setup.local
# RedHat patch to adapt python to RedHat layout, mostly lib -> lib64
RUN cd cpython && patch -p1 < /tmp/00102-lib64.patch && ./configure && make -j`expr 2 \* $(nproc)` | tee bear.out
COPY platform_uname.patch platform_uname.patch
# Note: this patch also needs to be applied in 'make fromsrc'- see ./Makefile
RUN patch -p0 < platform_uname.patch

WORKDIR /home/$USER/cpython
COPY extensions/ ../extensions/
# prepare "default" .so extensions to be statically linked in, and save neccessary files
# .orig MUST be the suffix of original python we are extracting symbols from
RUN mv python python.orig \
   && ../extensions/prepare_extension.py python.orig  --log=quiet --self \
   && ../extensions/prepare_extension.py bear.out --no_mung --log=quiet --skip ../extensions/skip_builtins.txt \
   && files="dlstatic_km.mk build/temp.* `find build -name '*.km.*'` `find build -name '*\.so'`" ; tar cf - $files \
   | (mkdir builtins; tar -C builtins -xf -)

# Build the target image
FROM kontainapp/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION}
ENV PYTHONTOP=/home/$USER/cpython
#
# The following copies two sets of artifacts - objects needed to build (link) python.km,
# and the sets of files to run set of tests. The former is used by link-km.sh,
# the latter is copied to the output by Makefile using NODE_DISTRO_FILES.

# Python VERS and ABI flags **MUST** be passed from upstairs
ARG VERS
ARG ABI

RUN BUILD_LOC="/home/$USER/cpython/build/lib.linux-x86_64-${VERS}/ /home/$USER/cpython/build/temp.linux-x86_64-${VERS}"; \
   mkdir -p ${BUILD_LOC} && chown $USER ${BUILD_LOC}
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/builtins cpython/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/python.km.symbols.c cpython/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Lib/ cpython/Lib/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Modules/ cpython/Modules/
COPY --from=buildenv-cpython --chown=appuser:appuser \
   /home/$USER/cpython/build/lib.linux-x86_64-${VERS}/_sysconfigdata_${ABI}_linux_x86_64-linux-gnu.py \
   cpython/build/lib.linux-x86_64-${VERS}/_sysconfigdata_${ABI}_linux_x86_64-linux-gnu.py
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/build/temp.linux-x86_64-${VERS} cpython/build/temp.linux-x86_64-${VERS}/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Programs/python.o cpython/Programs/python.o
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/libpython${VERS}${ABI}.a cpython/
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/pybuilddir.txt cpython/
