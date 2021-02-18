# Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)

EXEC := km_cli
SOURCES := km_client.c
VERSION_SRC := km_client.c # it has branch/version info, so rebuild it if git info changes
#INCLUDES := ${TOP}/include ${TOP}/km
#LLIBS := elf z
LOCAL_COPTS := -Werror
LOCAL_LDOPTS := -Wl,--script=${TOP}/km/km_guest_ldcmd

include ${TOP}/make/actions.mk

install: ${BLDEXEC} ## Install into /opt/kontain
	sudo cp ${BLDEXEC} ${KM_OPT_BIN}/${EXEC}
