#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
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
