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

ENV KM_TOP=/home/appuser/km
ENV KM_TEST_TOP=${KM_TOP}/tests

USER root

RUN mkdir -p ${KM_TOP}
RUN mkdir ${KM_TOP}/build

COPY --chown=appuser:appuser . ${KM_TEST_TOP}
ADD extras.tar.gz ${KM_TOP}/build/

RUN chown -R appuser:appuser ${KM_TOP}/build

WORKDIR /home/appuser/km/tests

ENV PATH=${KM_TOP}/build/opt/kontain/bin:${KM_TEST_TOP}/bats/bin:.:$PATH
ENV TIME_INFO ${KM_TEST_TOP}/time_info.txt

USER appuser
