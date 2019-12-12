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
# Dockerfile to package node.km and friends for testing in CI/CD

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest
FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}


ARG MODE=Release
ENV MODE=$MODE VERS=$VERS NODETOP=/home/appuser/node

COPY --chown=appuser:appuser node /home/$USER/node
COPY --chown=appuser:appuser skip_* km /home/$USER/
COPY --chown=appuser:appuser scripts /home/$USER/scripts
RUN echo '#!/home/'$USER'/km --copyenv' > /home/$USER/node/out/${MODE}/node && chmod +x /home/$USER/node/out/${MODE}/node

WORKDIR /home/$USER/
