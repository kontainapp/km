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
# Dockerfile for tests.

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ARG branch
ENV BRANCH=${branch}

USER root

RUN test -f km.coverage && cp km.coverage /opt/kontain/coverage/bin/km || true

ENV KM_TOP=/home/appuser/km
ENV KM_TEST_TOP=${KM_TOP}/tests
RUN mkdir -p ${KM_TOP}
RUN chown appuser ${KM_TOP}
COPY --chown=appuser:appuser . ${KM_TEST_TOP}
ADD --chown=appuser:appuser ./bldenv.tgz ${KM_TOP}
# Due to Docker issue https://github.com/docker/for-linux/issues/360
RUN chown -R appuser:appuser ${KM_TOP}/build

WORKDIR /home/appuser/km/tests
# this is needed for symlinks to work
# RUN mkdir -p ../build/km ; cp ${KM_TOP}/build/opt_kontain/bin/km ../build/km

ENV PATH=${KM_TEST_TOP}/bats/bin:.:$PATH
ENV TIME_INFO ${KM_TEST_TOP}/time_info.txt

USER appuser