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

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ARG branch
ENV BRANCH=${branch}

COPY libc.so /opt/kontain/runtime/libc.so
COPY km /opt/kontain/bin/km
COPY km.coverage /opt/kontain/bin/coverage/km

ENV KM_TOP=/home/appuser/km
ENV KM_TEST_TOP=${KM_TOP}/tests
ENV KM_LDSO_PATH /opt/kontain/runtime:/opt/kontain/alpine-lib/usr/lib
RUN mkdir -p ${KM_TOP}
RUN chown appuser ${KM_TOP}
COPY --chown=appuser:appuser . ${KM_TEST_TOP}
WORKDIR /home/appuser/km/tests

ENV PATH=${KM_TEST_TOP}}/bats/bin:.:$PATH
ENV TIME_INFO ${KM_TEST_TOP}}/time_info.txt