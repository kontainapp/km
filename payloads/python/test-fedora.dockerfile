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
FROM kontain/buildenv-km-${DTYPE}

ENV PHOME /home/$USER/python/

RUN mkdir -p ${PHOME}/cpython ${PHOME}/cpython/build/lib.linux-x86_64-3.7/
WORKDIR ${PHOME}

COPY --chown=appuser:appuser scripts scripts/
COPY --chown=appuser:appuser cpython/pybuilddir.txt cpython/
COPY --chown=appuser:appuser km cpython/python.km test_unittest.py ./
COPY --chown=appuser:appuser cpython/Modules cpython/Modules/
COPY --chown=appuser:appuser cpython/Lib cpython/Lib/
COPY --chown=appuser:appuser cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py cpython/Lib/
RUN echo '#!/usr/bin/env -S '${PHOME}'km --putenv=PYTHONPATH='${PHOME}'cpython/Lib --putenv=PYTHONHOME=foo:foo' > python && chmod +x ./python

