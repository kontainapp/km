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
# Dockerfile to package python.km and friends for testing in CI/CD

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

#  Python version and ABI flag **MUST** be passed on build
ARG VERS
ARG ABI
ENV PHOME /home/$USER/python/

RUN mkdir -p ${PHOME}/cpython ${PHOME}/cpython/build/lib.linux-x86_64-${VERS}
WORKDIR ${PHOME}

COPY --chown=appuser:appuser scripts scripts/
COPY --chown=appuser:appuser test_snapshot.py test_snapshot.py
COPY --chown=appuser:appuser cpython/pybuilddir.txt cpython/
COPY --chown=appuser:appuser km cpython/python.km cpython/python.kmd.mimalloc test_unittest.py ./
COPY --chown=appuser:appuser cpython/Modules cpython/Modules/
COPY --chown=appuser:appuser cpython/Lib cpython/Lib/
# TODO: construct path names once, instread of hardcoding them here
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-${VERS} cpython/build/lib.linux-x86_64-${VERS}
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-${VERS}/_sysconfigdata_${ABI}_linux_x86_64-linux-gnu.py cpython/Lib/
RUN ln -s km python && echo -e "home = ${PHOME}/cpython\ninclude-system-site-packages = false\nruntime = kontain" > pyvenv.cfg
ENV KM_VERBOSE="GENERIC"

