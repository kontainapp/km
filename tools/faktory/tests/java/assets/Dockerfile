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
FROM adoptopenjdk/openjdk11:alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM adoptopenjdk/openjdk11:alpine-jre
WORKDIR /app
ARG APPJAR=/app/target/*.jar
# TODO: next two line are introduce a foreknowledge of kontain java. Need to do that in the converter
ENV PATH ${PATH}:/opt/kontain/java/bin
COPY --from=builder /tmp /tmp

COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java", "-jar", "app.jar"]
