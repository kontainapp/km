#
#  Copyright © 2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Take $FROM_IMAGE and convert it to run payload (kafka/zookeeper) in a Kontain VM
#
# TODO: assumes mounts pointing to image with .so. We need to either test with existing .so, or to copy
# Kontain-built ones instead of volume mounts

ARG FROM_IMAGE
FROM $FROM_IMAGE

ARG KDIR
ARG KONTAIN_JAVA_DIR

ENV JAVA_HOME ${KONTAIN_JAVA_DIR}
# TODO: fix JIT support. For now turning it off
ENV KAFKA_OPTS=-Djava.compiler

RUN mkdir -p ${KONTAIN_JAVA_DIR}/bin
ADD ${KDIR}/ ${KONTAIN_JAVA_DIR}/bin/