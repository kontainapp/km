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
IMAGE_NAME := docker.io/kontainapp/helloworld-nodejs:latest

build:
	cp ${KM_BIN} . 
	docker build -t ${IMAGE_NAME} .
	rm -rf km

push:
	docker push ${IMAGE_NAME}

deploy:
	kubectl apply -f service.yaml

delete:
	kubectl delete -f service.yaml

clean:
	-docker rmi ${IMAGE_NAME}
	rm -rf km

include ${TOP}/make/locations.mk
