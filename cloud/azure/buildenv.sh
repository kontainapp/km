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
# create buildenv images and push them to Azure regitry

TOP=$(git rev-parse --show-cdup)
source `dirname $0`/cloud_config.mk

DTYPE=$1
DLOC=${TOP}docker/build
DIMG=km-buildenv-${DTYPE}
DFILE=Dockerfile.${DTYPE}
DIMG=${REGISTRY}/${DIMG}

# DEBUG=echo

$DEBUG az acr login -n ${REGISTRY_NAME}
$DEBUG docker build --build-arg=USER=appuser  \
	--build-arg=UID=1001 --build-arg=GID=117 \
	-t ${DIMG} ${DLOC} -f ${DLOC}/${DFILE}
	docker push ${DIMG}
