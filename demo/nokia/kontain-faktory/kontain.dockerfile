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

ARG KONTAIN_DIR
ARG ORIG_JDK_DIR
ENV LD_LIBRARY_PATH ${ORIG_JDK_DIR}/lib/server:${ORIG_JDK_DIR}/lib/jli:${ORIG_JDK_DIR}/lib:${KONTAIN_DIR}/lib64
#:/lib64

ADD lib64 ${KONTAIN_DIR}/lib64
ADD runtime ${KONTAIN_DIR}/runtime
ADD env /usr/bin/
# TODO: We need to force-override bin and lib, but we need to keep old files in conf...
# The current code may kill conf customization, e.g. for logs
# RUN rm -r $ORIG_JDK_DIR/{lib,bin}/*  $ORIG_JDK_DIR/release
RUN rm -r $ORIG_JDK_DIR/lib/*  $ORIG_JDK_DIR/release
ADD km_java_files.tar $ORIG_JDK_DIR
RUN ln -s $ORIG_JDK_DIR/bin/java.km /usr/bin
# On the host, do this: echo '/tmp/core.%h.%e.%t' > /proc/sys/kernel/core_pattern ; ulimit -c unlimited