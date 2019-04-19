#
#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Basic build for kontain payloads

# scan all these and  'make' stuff there
SUBDIRS := python

# make sure payloads dir is skipped for top-level 'make' to avoid rebuilds when only KM is needed
# to run, use 'make all'
default: 
	@echo "Skipping payloads build for goal '$@'. Use 'make all' or 'make build' for payloads build"

# use this target to force payloads build
build: $(SUBDIRS)

TOP := $(shell git rev-parse --show-cdup)
include ${TOP}make/actions.mk
