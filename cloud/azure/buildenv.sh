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
# create buildenv images and push them to Azure registry

TOP=$(git rev-parse --show-toplevel)
source `dirname $0`/cloud_config.mk

DTYPE=$1
BUILDENV_PATH=${TOP}/docker/build
BUILDENV_DOCKERFILE=${TOP}/tests/buildenv-${DTYPE}.dockerfile
BUILDENV_KM_IMG=${REGISTRY}/buildenv-km-${DTYPE}

# DEBUG=echo

$DEBUG az acr login -n ${REGISTRY_NAME}
$DEBUG docker build --build-arg=USER=appuser  \
	-t ${BUILDENV_KM_IMG} ${BUILDENV_PATH} -f ${BUILDENV_DOCKERFILE}
docker push ${BUILDENV_KM_IMG}
