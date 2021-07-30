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
# Dockerfile for build node.js image. There are two stages:
#
# buildenv-node - based on kontainapp/buildenv-km-fedora, git clone, compile and test node
# linkenv - based on km-build-env and just copy the objetcs and test files

ARG MODE=Release
ARG VERS=v12.4.0
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION} AS buildenv-node
ARG MODE
ARG VERS

RUN git clone https://github.com/nodejs/node.git -b $VERS
RUN cd node && ./configure --gdb `[[ $MODE == Debug ]] && echo -n --debug` && make -j`expr 2 \* $(nproc)` && make jstest

FROM kontainapp/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION}
ARG MODE
ARG VERS
ENV MODE=$MODE VERS=$VERS NODETOP=/home/appuser/node
#
# The following copies two sets of artifacts - objects needed to build (link) node.km,
# and the sets of files to run set of tests. The former is used by link-km.sh,
# the latter is copied to the output by Makefile using PYTHON_DISTRO_FILES.
#
COPY --from=buildenv-node --chown=appuser:appuser /home/$USER/node/out/ node/out
COPY --from=buildenv-node --chown=appuser:appuser /home/$USER/node/test/ node/test
COPY --from=buildenv-node --chown=appuser:appuser /home/$USER/node/tools/ node/tools
COPY --from=buildenv-node --chown=appuser:appuser /home/$USER/node/doc/ node/doc
COPY --from=buildenv-node --chown=appuser:appuser /home/$USER/node/deps/npm/ node/deps/npm