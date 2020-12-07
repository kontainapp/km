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
# Dockerfile to package demo-dweb for testing

ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ENV DHOME /home/$USER/demo-dweb/dweb

RUN mkdir -p ${DHOME}
WORKDIR ${DHOME}

COPY --chown=appuser:appuser km dweb/dweb dweb/js/  dweb/css/ dweb/fonts/ dweb/*.htm* dweb/*.png dweb/*.ico ${DHOME}/
RUN ln -s km demo-dweb && ln -s dweb demo-dweb.km

EXPOSE 8080/tcp
ENV KM_VERBOSE="GENERIC"

