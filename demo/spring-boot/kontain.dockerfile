# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


FROM kontainapp/runenv-jdk-11.0.8:latest
ARG TARGET_JAR_PATH
COPY ${TARGET_JAR_PATH} /app.jar
COPY run.sh run_snap.sh /
ADD empty_tmp /tmp/
EXPOSE 8080/tcp
CMD ["/opt/kontain/java/bin/java", "-XX:-UseCompressedOops", "-jar", "/app.jar"]
