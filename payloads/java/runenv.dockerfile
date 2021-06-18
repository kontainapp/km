# Copyright © 2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Create runenv for Java KM

ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-dynamic-base:${RUNENV_IMAGE_VERSION}

ARG JAVA_DIR=/opt/kontain/java
ENV LD_LIBRARY_PATH ${JAVA_DIR}/lib/server:${JAVA_DIR}/lib/jli:${JAVA_DIR}/lib:/opt/kontain/runtime
COPY . ${JAVA_DIR}/
