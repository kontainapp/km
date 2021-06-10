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