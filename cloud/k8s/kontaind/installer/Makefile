#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
# 
# The image will be used to install kontain artifacts on each node.

TOP := $(shell git rev-parse --show-toplevel)
CURRENT := ${TOP}/cloud/k8s/kontaind/installer

COMPONENT = kontain-installer
IMAGE_VERSION ?= latest

.PHONY: clean
clean:
	rm -rf ${RUNENV_PATH}

# runenv-image configs
export define runenv_prep
	mkdir -p ${RUNENV_PATH}/kontain/bin
	cp ${KM_BIN} ${RUNENV_PATH}/kontain/bin/km
	cp installer.sh ${RUNENV_PATH}/installer.sh
endef

include ${TOP}/make/images.mk
