# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

# turn off km symlink trick and minimal shell interpretation
ENV KM_DO_SHELL NO
ADD --chown=0:0 busybox/_install run-test.sh run-all-tests.sh ./
ADD km .
