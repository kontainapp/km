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
# Dockerfile to package python.km and friends for testing in CI/CD

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ENV PHOME /home/$USER/python/

RUN mkdir -p ${PHOME}/cpython ${PHOME}/cpython/build/lib.linux-x86_64-3.7/
WORKDIR ${PHOME}

COPY --chown=appuser:appuser scripts scripts/
COPY --chown=appuser:appuser cpython/pybuilddir.txt cpython/
COPY --chown=appuser:appuser km libc.so cpython/python.km test_unittest.py ./
COPY --chown=appuser:appuser cpython/Modules cpython/Modules/
COPY --chown=appuser:appuser cpython/Lib cpython/Lib/
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-3.7 cpython/build/lib.linux-x86_64-3.7
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py cpython/Lib/
RUN ln -s km python
ENV PYTHONPATH=${PHOME}cpython/Lib:${PHOME}/cpython/build/lib.linux-x86_64-3.7 PYTHONHOME=foo:foo
ENV KM_VERBOSE="GENERIC"

