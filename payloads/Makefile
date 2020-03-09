#
#  Copyright © 2019 Kontain Inc. All rights reserved.
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
SUBDIRS := python demo-dweb node
#  Note about ./busybox - we don't use busybox and there are build issues. Dropping for now.

# Filter out 'make test' requests' to assure that 'make test' only builds/tests KM. If payload needs to be tested, uses 'make test-all'
# Also, just 'make' will not cause building payloads , so it is reserved for building KM only. Use 'make all' or 'make build' to request payloads build
default test coverage:
	@echo "Info: ignoring target '$@' in payloads"

# use this target to force payloads build
link build: $(SUBDIRS)

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk
