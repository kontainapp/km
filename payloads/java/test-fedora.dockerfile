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

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

# Dedicated arbitrary in-image uid/gid, same as in km buildenv
ARG UID=1001
ARG GID=117

# Contains the scripts needs to run tests.
ADD --chown=$UID:$GID extras.tar.gz /home/appuser/km
ARG JAVA_DIR=/home/appuser/km/build/opt/kontain
ARG BLDDIR=/home/appuser/km/build/
ENV PATH=${JAVA_DIR}/bin:${PATH}
COPY . ${JAVA_DIR}
RUN mkdir -p /home/appuser/km/payloads/java
COPY scripts/* /home/appuser/km/payloads/java/scripts/
COPY bats/ /home/appuser/km/payloads/java/scripts/bats/
COPY bats-assert/ /home/appuser/km/payloads/java/scripts/bats-assert/
COPY bats-support/ /home/appuser/km/payloads/java/scripts/bats-support/
COPY run_bats_tests.sh /home/appuser/km/payloads/java/scripts/
COPY test_java.bats /home/appuser/km/payloads/java/scripts/
COPY payloads/java/mods/ ${BLDDIR}/mods/
COPY payloads/java/lib/ ${BLDDIR}/lib/
COPY sb.jar /home/appuser/km/payloads/java/

ENV JAVA_DIR=${JAVA_DIR}
ENV BLDDIR=${BLDDIR}
ENV JAVA_LD_PATH=${JAVA_DIR}/lib/server:${JAVA_DIR}/lib/jli:${JAVA_DIR}/lib:/opt/kontain/runtime
ENV PATH=${JAVA_DIR}/bin:${PATH}

WORKDIR /home/appuser/km/payloads/java