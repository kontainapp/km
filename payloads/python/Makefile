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

# Location of cpython sources. When building in container, it is set in buildenv dockerfile.
PYTHONTOP ?= cpython

# component id for docker images
COMPONENT := python

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.km

# name to be added to label(s)
PAYLOAD_NAME := Python3.7

# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' --exclude='*/__pycache__/*' --exclude '*.exe' --exclude '*.whl' \
	cpython/python.km cpython/Lib cpython/Modules/Setup scripts

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
PYTHON_DISTRO_FILES := $(addprefix cpython/, Lib Modules build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py pybuilddir.txt)

# either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifneq ($(shell test -d cpython/.git && echo OK),OK)
	${DOCKER_RUN} -v $(realpath ${TOP}):/src:Z -w /src kontain/buildenv-python-fedora make -C payloads/python in-blank-container
else
	./link-km.sh ${PYTHONTOP} cpython
endif

in-blank-container: ## invoked in blank contaier by ``make all''. DO NOT invoke manually
	tar -C ${PYTHONTOP}/.. -cf - ${PYTHON_DISTRO_FILES} | tar -xf -
	cd cpython && patch -p1 < ../unittest.patch
	./link-km.sh ${PYTHONTOP} cpython

fromsrc:
	git clone https://github.com/python/cpython.git -b ${VERS}
	cd ${PYTHONTOP} && ./configure && cp ../Setup.local Modules/Setup.local && make -j`expr 2 \* $(nproc)`
	cd ${PYTHONTOP} && patch -p1 < ../unittest.patch
	./link-km.sh ${PYTHONTOP} ${PYTHONTOP}

clobber:
	rm -rf cpython

clean:
	@echo "Nothing to do for 'clean' - use 'clobber' to force remove build artifacts"
	@# TODO - clean ./cpython build artifacts

test test-all: ${PAYLOAD_KM} ${TEST_KM}
	scripts/test-run.sh ${KM_BIN} ${PAYLOAD_KM}

test-withdocker test-all-withdocker:
	${DOCKER_RUN_TEST} ${TEST_IMG}:${IMAGE_VERSION} scripts/test-run.sh ./python

CONTAINER_TEST_CMD := "./scripts/test-run.sh", "./python"
test-withk8s : .test-withk8s  ##

# if something needs to be copied/modified before building runenv image in Docker,
# define this function
# TODO (fix it): runenv-image for python only works with ENTRYPOINT override: --entrypoint /python
# Also it's HUGE (170MB), need stuff stripped
export define runenv_prep
	tar -cf - ${PYTHON_DISTRO_FILES} scripts | tar -C $(RUNENV_PATH) -xf -
	echo '#!/km --putenv=PYTHONPATH=/cpython/Lib' > $(RUNENV_PATH)/python ; chmod a+x $(RUNENV_PATH)/python
endef

include ${TOP}make/images.mk
