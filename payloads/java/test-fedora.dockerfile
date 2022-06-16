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
FROM fedora:31

# Contains the scripts needs to run tests.
ADD extras.tar.gz /home/appuser/km/build/
ARG JAVA_DIR=/home/appuser/km/build/opt/kontain
ENV PATH=${JAVA_DIR}/bin:${PATH}
COPY . ${JAVA_DIR}
RUN mkdir -p /home/appuser/km/payloads/java
COPY ./scripts/* /home/appuser/km/payloads/java/scripts/

ENV JAVA_DIR=${JAVA_DIR}
ENV JAVA_LD_PATH=${JAVA_DIR}/lib/server:${JAVA_DIR}/lib/jli:${JAVA_DIR}/lib:/opt/kontain/runtime
ENV PATH=${JAVA_DIR}/bin:${PATH}

WORKDIR /home/appuser/km/payloads/java