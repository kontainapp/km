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
COPY --chown=appuser:appuser km libc.so cpython/python.km cpython/python.kmd test_unittest.py ./
COPY --chown=appuser:appuser cpython/Modules cpython/Modules/
COPY --chown=appuser:appuser cpython/Lib cpython/Lib/
# TODO: construct path names once, instread of hardcoding them here
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-${VERS} cpython/build/lib.linux-x86_64-${VERS}
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-${VERS}/_sysconfigdata_${ABI}_linux_x86_64-linux-gnu.py cpython/Lib/
RUN ln -s km python && echo -e "home = ${PHOME}/cpython\ninclude-system-site-packages = false\nruntime = kontain" > pyvenv.cfg
ENV KM_VERBOSE="GENERIC"

