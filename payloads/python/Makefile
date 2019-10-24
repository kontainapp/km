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
# Makefile for building and packing Python payload runtime for KM

TOP := $(shell git rev-parse --show-cdup)
VERS ?= v3.7.4
SHELL=/bin/bash

# Location of cpython sources. When building in container, it is set in buildenv dockerfile.
PYTHONTOP ?= cpython

# component id for docker images
COMPONENT := python

# this is how we run python,km tests in container
CONTAINER_TEST_CMD := ./scripts/test-run.sh ./python

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.km

# name to be added to label(s)
PAYLOAD_NAME := Python3.7

# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' --exclude='*/__pycache__/*' --exclude '*.exe' --exclude '*.whl' \
	cpython/python.km cpython/Lib cpython/Modules/Setup scripts

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
PYTHON_DISTRO_FILES := $(addprefix cpython/, Lib Modules build/lib.linux-x86_64-3.7 pybuilddir.txt dlstatic_km.mk)

# either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifneq ($(shell test -d cpython/.git && echo OK),OK)
	${DOCKER_RUN} -v $(realpath ${TOP}):/src:Z -w /src ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} make -C payloads/python in-blank-container
else
	 make -C cpython -f dlstatic_km.mk
	./extensions/build_extensions.sh
	./link-km.sh ${PYTHONTOP} cpython
endif

in-blank-container: ## invoked in blank container by ``make all''. DO NOT invoke manually
	tar -C ${PYTHONTOP}/.. -cf - ${PYTHON_DISTRO_FILES} | tar -xf -
	cd cpython && patch -p1 < ../unittest.patch
	make -C cpython -f dlstatic_km.mk
	@echo "*** Warning - module extension are skipped. Use 'fromsrc' build for extensions"
	./link-km.sh ${PYTHONTOP} cpython

fromsrc:
	git clone https://github.com/python/cpython.git -b ${VERS}
	cd cpython && ./configure && make -j`expr 2 \* $(nproc)` | tee bear.out
	cd cpython && patch -p1 < ../unittest.patch
	cd cpython && ../extensions/prepare_extension.py bear.out --skip ../extensions/skip_builtins.txt
	# this seems repetitive, TODO - clean cut-n-paste:
	make -C cpython -f dlstatic_km.mk
	./extensions/build_extensions.sh
	./link-km.sh cpython cpython

clean:
	@echo "Nothing to do for 'clean' - use 'clobber' to force remove build artifacts"
	@# TODO - clean ./cpython build artifacts

test test-all: ${PAYLOAD_KM} ${TEST_KM}
	scripts/test-run.sh ${KM_BIN} ${PAYLOAD_KM}

test-all-withdocker: test-withdocker # alias for test-withdocker

# if something needs to be copied/modified before building runenv image in Docker,
# define this function
# TODO (fix it): runenv-image for python only works with ENTRYPOINT override: --entrypoint /python
# Also it's HUGE (170MB), need stuff stripped
export define runenv_prep
	tar -cf - ${PYTHON_DISTRO_FILES} scripts | tar -C $(RUNENV_PATH) -xf -
	echo '#!/km --putenv=PYTHONPATH=/cpython/Lib' > $(RUNENV_PATH)/python ; chmod a+x $(RUNENV_PATH)/python
endef
clobber:
	rm -rf cpython

clobber-modules:
	cd cpython; git clean -xdff Lib Modules

include ${TOP}make/images.mk
