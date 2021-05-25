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

# Use alpine for now. It only adds 6MB to Java's 170MB, and allows us to use 'ln -s' below
# TODO: switch to 'from scratch'. See https://github.com/kontainapp/km/issues/496
FROM alpine

ARG JAVA_DIR=/opt/kontain/java
ENV LD_LIBRARY_PATH ${JAVA_DIR}/lib/server:${JAVA_DIR}/lib/jli:${JAVA_DIR}/lib:/opt/kontain/runtime:/lib64

COPY java ${JAVA_DIR}/
COPY runtime /opt/kontain/runtime/

RUN \
   ln -s /opt/kontain/runtime/libc.so /opt/kontain/runtime/ld-linux-x86-64.so.2 && \
   ln -s /opt/kontain/bin/km  ${JAVA_DIR}/bin/java && \
   cd ${JAVA_DIR}/bin/; ln -s  java.kmd java.km && \
   ln -s ${JAVA_DIR}/bin/java /bin/java

ENTRYPOINT [ "java" ]
