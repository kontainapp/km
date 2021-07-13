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
# Dockerfile to package demo-dweb for testing

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ENV DHOME /home/$USER/demo-dweb/dweb

RUN mkdir -p ${DHOME}
WORKDIR ${DHOME}

COPY --chown=appuser:appuser km dweb/dweb dweb/js/  dweb/css/ dweb/fonts/ dweb/*.htm* dweb/*.png dweb/*.ico ${DHOME}/
RUN ln -s km demo-dweb && ln -s dweb demo-dweb.km

EXPOSE 8080/tcp
ENV KM_VERBOSE="GENERIC"

