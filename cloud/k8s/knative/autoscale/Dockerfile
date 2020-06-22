# Copyright 2018 The Knative Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FROM golang AS builder

WORKDIR /go/src/app
COPY . /go/src/app
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags "-T 0x201000 -w -extldflags '-no-pie -static -Wl,--gc-sections'" -o /bin/autoscale.km autoscale.go

FROM gcr.io/distroless/base
EXPOSE 8080
COPY --from=builder /bin/autoscale.km /opt/kontain/bin/autoscale.km
COPY km /opt/kontain/bin/km
ENTRYPOINT ["/opt/kontain/bin/km", "/opt/kontain/bin/autoscale.km"]