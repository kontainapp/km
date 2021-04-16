# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#

TOP := $(shell git rev-parse --show-toplevel)

include ${TOP}/make/actions.mk

all: kontain-gcc
	cp kontain-gcc ${KM_OPT_BIN_PATH}
	ln -f -s ${KM_OPT_BIN_PATH}/kontain-gcc ${KM_OPT_BIN_PATH}/kontain-g++

