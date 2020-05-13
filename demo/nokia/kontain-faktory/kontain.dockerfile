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

ARG FROM_IMAGE
FROM $FROM_IMAGE

ARG KONTAIN_DIR=/opt/kontain
ARG ORIG_JDK_DIR
ARG JDK_VERSION
ENV LD_LIBRARY_PATH ${ORIG_JDK_DIR}/lib/server:${ORIG_JDK_DIR}/lib/jli:${ORIG_JDK_DIR}/lib:${KONTAIN_DIR}/lib64:${KONTAIN_DIR}/runtime

# Drop files overriden by Kontain java, and dev time jmods, and THEN install ours
RUN rm -r $ORIG_JDK_DIR/lib/*  $ORIG_JDK_DIR/release && \
   yum erase -y java-11-openjdk-jmods java-11-openjdk-devel \
   dejavu-sans-fonts && \
   yum clean all
ADD $JDK_VERSION $ORIG_JDK_DIR

# Add Kontain generic libs and dynlinker
ADD alpine-lib ${KONTAIN_DIR}/alpine-lib
ADD runtime ${KONTAIN_DIR}/runtime

# Make sure libjvm.so finds the NEEDED ld-linux; and that km is in the right place
# TODO: remove the need for ld-linux, plus place km & libc.so in the right place - see copy_needed_files.sh
RUN ln -d /opt/kontain/runtime/libc.so /opt/kontain/runtime/ld-linux-x86-64.so.2 ; \
   mkdir $KONTAIN_DIR/bin; mv $ORIG_JDK_DIR/bin/km $KONTAIN_DIR/bin/km

# Make sure 'shebang' can find /usr/bin/java.km if needed
RUN ln -s $ORIG_JDK_DIR/bin/java.km /usr/bin/java.km;

# Note: to retain core dumps in /tmp, do this on the host:
# echo '/tmp/core.%h.%e.%t' > /proc/sys/kernel/core_pattern ; ulimit -c unlimited