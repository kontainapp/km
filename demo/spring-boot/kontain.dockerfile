#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
FROM kontainapp/runenv-jdk-shell-11.0.8:latest
ARG TARGET_JAR_PATH
COPY ${TARGET_JAR_PATH} /app.jar
COPY run.sh run_snap.sh /
EXPOSE 8080/tcp
ENV KM_MGTPIPE=/tmp/km.sock
CMD ["java", "-XX:-UseCompressedOops", "-jar", "/app.jar"]
