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

# TODO - ln -s /opt/kontain/runtime/libc.so /opt/kontain/runtime/ ld-linux-x86-64.so.2

FROM alpine
# Note: alpine is 6MB so from alpine is 177MB and from scratch is 171 mb. Both work

ARG FROM=jdk-11.0.6+10/build/linux-x86_64-normal-server-release/images/jdk
ARG JAVA_DIR=/opt/kontain/java

COPY jdk-11.0.6+10/build/linux-x86_64-normal-server-release/images/jdk/ ${JAVA_DIR}/
COPY Hello.* /tmp/

ENTRYPOINT [ "/opt/kontain/bin/km", \
   "--putenv=LD_LIBRARY_PATH=/opt/kontain/java/lib/server:/opt/kontain/java/lib/jli:/opt/kontain/java/lib:/opt/kontain/runtime", \
   "/opt/kontain/java/bin/java.kmd" ]