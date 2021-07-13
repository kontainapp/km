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


TOP := $(shell git rev-parse --show-toplevel)
IMAGE_NAME := docker.io/kontainapp/autoscale:latest

payload-build:
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags "-T 0x201000 -w -extldflags '-no-pie -static -Wl,--gc-sections'" -o autoscale.km autoscale.go

image-build:
	cp ${KM_BIN} . 
	docker build -t ${IMAGE_NAME} .
	rm -rf km

image-push:
	docker push ${IMAGE_NAME}

clean:
	-docker rmi ${IMAGE_NAME}
	rm -rf km
	rm -rf autoscale.km

include ${TOP}/make/locations.mk
