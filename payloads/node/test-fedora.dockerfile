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
# Dockerfile to package node.km and friends for testing in CI/CD

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}


ARG MODE=Release
ENV MODE=$MODE VERS=$VERS NODETOP=/home/appuser/node

COPY --chown=appuser:appuser node /home/$USER/node
ADD --chown=appuser:appuser ./extras.tar.gz /home/$USER/km/build
COPY --chown=appuser:appuser scripts /home/$USER/scripts
COPY --chown=appuser:appuser skip_* /home/$USER/
COPY --chown=appuser:appuser platform:f35_skip /home/$USER/

ENV PATH "$PATH:/home/appuser/km/build/opt/kontain/bin"

WORKDIR /home/$USER/
