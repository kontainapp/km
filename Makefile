#
# Basic build for kontain machine (km) and related tests/examples.
#
# See README.md for details. run 'make help' for help.
# Dependencies are enumerated in  tests/buildenv-fedora.dockerfile.
# See 'make -C tests buildenv-fedora-local' for installing dependencies
#
# ====
#  Copyright © 2018-2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)

# scan all these and 'make' stuff there
SUBDIRS := lib km km_cli runtime tests payloads container-runtime tools/faktory tools/bin include

# build VMM and runtime library before trying to build tests
tests: km runtime container-runtime lib

# On mac $(MAKE) evaluates to '/Applications/Xcode.app/Contents/Developer/usr/bin/make'
ifeq ($(shell uname), Darwin)
MAKE := make
endif

kkm-pkg: ## Build KKM module self-extracting package.
	@if ! type makeself >& /dev/null ; then echo Please install \"makeself\" first; false; fi
	@# We only needs source code, so running 'clean' first
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP}/kkm/kkm clean
	makeself -q kkm ${BLDTOP}/kkm.run "beta-release" ./installer/build-script.sh

RELEASE_TAG ?= v0.1-edge
RELEASE_MESSAGE ?= Kontain KM Edge - date: $(shell date) sha: $(shell git rev-parse HEAD)
REPO_URL := https://${GITHUB_TOKEN}@github.com/kontainapp/km.git
edge-release: ## Trigger edge-release building pipeline
	git config user.email "release@kontain.app"
	git config user.name "Kontain Release Pipeline Bot"
	@echo Delete the ${RELEASE_TAG} tag. Can fail if there is no such tag yet
	-git tag -d ${RELEASE_TAG} && git push --delete ${REPO_URL} ${RELEASE_TAG}
	@echo Now tag the source and push the tag to trigger the pipeline
	git tag -a ${RELEASE_TAG} --message "${RELEASE_MESSAGE}"
	git push ${REPO_URL} ${RELEASE_TAG}


# Install git hooks, if needed
GITHOOK_DIR ?= .githooks
git-hooks-init: ## make git use GITHOOK_DIR (default .githooks) for pre-commit and other hooks
	git config core.hooksPath $(GITHOOK_DIR)
git-hooks-reset: ## reset git hooks location to git default of .git/hooks (not version controlled)
	git config core.hooksPath .git/hooks

compile-commands: ## rebuild compile_commands.json. Assumes 'bear' is installed
	make clean
	bear make -j
	sed -i 's-${CURDIR}-$${workspaceFolder}-' compile_commands.json

# clean in subdirs will be done automatically. This is clean for stuff created from this makefile
clean:
	rm -f ${BLDTOP}/kkm.run

include ${TOP}/make/actions.mk
