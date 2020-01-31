# Copyright © 2019-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile for tests.
# Usage will be 'docker run -it --rm --env BRANCH=$(git rev-parse --abbrev-ref HEAD) --device=/dev/kvm --user 0 test-km-fedora km_cdocker run <container> make TARGET=<target> - see ../../Makefile
#
ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ARG branch

ENV TIME_INFO /tests/time_info.txt
ENV KM_BIN /tests/km
ENV KM_LDSO /tests/libc.so
ENV KM_LDSO /opt/kontain/runtime/libc.so
ENV KM_LDSO_PATH /opt/kontain/lib64:/lib64
ENV BRANCH=${branch}

COPY --chown=appuser:appuser . /tests
COPY libc.so $PREFIX/runtime
RUN chmod 777 /tests

WORKDIR /tests
ENV PATH=/tests/bats/bin:.:$PATH
