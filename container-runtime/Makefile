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

TOP := $(shell git rev-parse --show-toplevel)

# Makefile for building and testing kontain's modified crun
# Since we are using crun's generated Makefile, this Makefile tries to adapt kontain's targets
# in actions.mk to the targets in the crun Makefile.
# "make all" builds crun
# "make test" runs crun's test suite

# This list of dependencies was taken from crun's README.md file.
# Note that we avoid using systemd-devl and golang-github-cpuguy83-md2man
# To check, use the .check_packages target from dep_check.mk
DEP_PACKAGES := python git gcc automake autoconf libcap-devel \
		yajl-devel libseccomp-devel \
		glibc-static python3-libmount libtool \
		openssl-libs openssl-devel
# The crun workspace is in the crun directory
CRUNDIR := crun
CRUNDIR_ABS := $(realpath $(CRUNDIR))

include ${TOP}/make/actions.mk

# Building crun shows errors indicating generated header files are not built yet,
# so force single threading the make.
all: $(CRUNDIR)/Makefile .makefile_check | ${KM_OPT_BIN}
	make -j1 -C $(CRUNDIR) all
	cp $(CRUNDIR)/crun ${KM_OPT_BIN}
	ln -f ${KM_OPT_BIN}/crun ${KM_OPT_BIN}/krun
	ln -f ${KM_OPT_BIN}/crun ${KM_OPT_BIN}/krun-label-trigger
	mkdir -p ${BLDDIR}
	cp $(CRUNDIR)/crun ${BLDDIR}/crun
	ln -f ${BLDDIR}/crun ${BLDDIR}/krun
	ln -f ${BLDDIR}/crun ${BLDDIR}/krun-label-trigger
	cp ./docker_config.sh ${KM_OPT_BIN}
	cp ./podman_config.sh ${KM_OPT_BIN}

$(CRUNDIR)/Makefile:
	cd $(CRUNDIR); ./autogen.sh; ./configure

static:
	[[ -z '$(ls -A /nix)' ]] && sudo docker run --rm --privileged -v /:/mnt nixos/nix:2.3.12 cp -rfT /nix /mnt/nix
	sudo docker run --rm --privileged -v /nix:/nix -v ${CRUNDIR_ABS}:${CRUNDIR_ABS} -w ${CRUNDIR_ABS} nixos/nix:2.3.12 nix-build --cores 0 --max-jobs auto --arg enableSystemd true nix/
	cp -f ${CRUNDIR_ABS}/result/bin/crun ${CRUNDIR_ABS}/krun.static
	ln -f ${CRUNDIR_ABS}/krun.static ${CRUNDIR_ABS}/krun.static-label-trigger

install-dev-runtime:
	@if [[ -z "${RUNTIME}" ]] || [[ "${RUNTIME}" = "krun" ]]; then \
		echo -e "$(RED)Parameter RUNTIME=<your name> is required and cannot be krun$(NOCOLOR)" && false; \
	fi
	sudo ./docker_config.sh --runtime-name=${RUNTIME} --runtime-path=${KM_OPT_BIN}/krun
	sudo ./podman_config.sh --runtime-name=${RUNTIME} --runtime-path=${KM_OPT_BIN}/krun

uninstall-dev-runtime:
	@if [[ -z "${RUNTIME}" ]] || [[ "${RUNTIME}" = "krun" ]]; then \
		echo -e "$(RED)Parameter RUNTIME=<your name> is required and cannot be krun$(NOCOLOR)" && false; \
	fi
	sudo ./docker_config.sh -u --runtime-name=${RUNTIME}
	sudo ./podman_config.sh -u --runtime-name=${RUNTIME}

# Run crun's tests
test:	all
	@if [ ! -x ${KM_OPT_KM} ] ; then \
		echo -e "${RED}Error: ${KM_OPT_KM} is not there or is not executable${NOCOLOR}"; false; \
	fi
	OCI_RUNTIME=${KM_OPT_BIN}/krun make -C $(CRUNDIR) check-TESTS
	OCI_RUNTIME=${KM_OPT_BIN}/crun make -C $(CRUNDIR) check-TESTS

clean::
	@if [ -f "$(CRUNDIR)/Makefile" ];then\
	   $(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C $(CRUNDIR) distclean;\
	fi
	@# crun's clean removes this file, so git sees it as a change. Restore back to clean
	cd $(CRUNDIR) && git checkout crun.1
	rm -f $(CRUNDIR)/configure $(CRUNDIR)/krun $(CRUNDIR)/krun.static $(CRUNDIR)/krun.static-label-trigger
	rm -f ${KM_OPT_BIN}/crun ${KM_OPT_BIN}/krun ${KM_OPT_BIN}/krun-label-trigger ${KM_OPT_BIN}/docker_config.sh ${KM_OPT_BIN}/podman_config.sh
	rm -f ${BLDDIR}/crun ${BLDDIR}/krun ${BLDDIR}/krun-label-trigger

# If they have a Makefile made for some other directory ask them to remake it
# Switching between docker builds and non-docker builds in the same dir can cause this.
.PHONY:	.makefile_check
.makefile_check: $(CRUNDIR)/Makefile
	@if ! grep -q "abs_builddir = $(CURDIR)" $(CRUNDIR)/Makefile ; then \
		echo -e "${RED}The generated crun/Makefile uses dir different from the current one ($(CURDIR))${NOCOLOR}"; \
		echo -e "${RED}Run 'make clobber' and then re-make${NOCOLOR}"; \
		false; \
	fi

include ${TOP}/make/dep_check.mk
