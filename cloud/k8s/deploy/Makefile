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
#
# The image will be used to install kontain artifacts on each node.

TOP := $(shell git rev-parse --show-toplevel)

include ${TOP}/make/locations.mk

all: 
	[ -d ${KM_OPT}/scripts ] || mkdir ${KM_OPT}/scripts
	cp k8s-deploy.sh ${KM_OPT}/scripts/k8s-deploy.sh
	docker build -t kontainapp/k8s-deploy:latest -f k8s-deploy.dockerfile ${KM_OPT}
	docker tag kontainapp/k8s-deploy:latest docker.io/kontainapp/k8s-deploy:latest
	docker push docker.io/kontainapp/k8s-deploy:latest

