#
# Basic build for kontain machine (km) and related tests/examples.
#
# See README.md for details. run 'make help' for help.
# Dependencies are enumerated in  tests/buildenv-fedora.dockerfile.
# See 'make -C tests buildenv-fedora-local' for installing dependencies
#
# ====
#  Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

# scan all these and  'make' stuff there
SUBDIRS := km runtime tests payloads

# build VMM and runtime library before trying to build tests
tests: km runtime

TOP := $(shell git rev-parse --show-toplevel)

# On mac $(MAKE) evaluates to '/Applications/Xcode.app/Contents/Developer/usr/bin/make'
ifeq ($(shell uname), Darwin)
MAKE := make
endif

# Install git hooks, if needed
GITHOOK_DIR ?= .githooks
git-hooks-init: ## make git use GITHOOK_DIR (default .githooks) for pre-commit and other hooks
	git config core.hooksPath $(GITHOOK_DIR)
git-hooks-reset: ## reset git hooks location to git default of .git/hooks (not version controlled)
	git config core.hooksPath .git/hooks

include ${TOP}/make/actions.mk
