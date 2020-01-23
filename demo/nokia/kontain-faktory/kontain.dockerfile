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
# Dockerfile to add KM and Java.km stuff to BASE container
#

ARG BASE
FROM $BASE

ARG KDIR
ARG TARGET_DIR

RUN mkdir -p ${TARGET_DIR}
ADD ${KDIR}/ ${TARGET_DIR}

ENTRYPOINT [ "/opt/kontain/java/bin/java" ]