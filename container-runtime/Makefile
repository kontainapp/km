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
		glibc-static python3-libmount libtool
# The crun workspace is in the crun directory
CRUNDIR := crun

include ${TOP}/make/actions.mk

# Building crun shows errors indicating generated header files are not built yet,
# so force single threading the make.
all:	$(CRUNDIR)/Makefile .makefile_check
	make -j$$(nproc) -C $(CRUNDIR) all

$(CRUNDIR)/Makefile:
	cd $(CRUNDIR); ./autogen.sh; ./configure --disable-systemd

# Run crun's tests
test:	all
	OCI_RUNTIME=$(CURDIR)/$(CRUNDIR)/crun make -C $(CRUNDIR) check-TESTS

clean::
	-$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C $(CRUNDIR) clean
	@# crun's clean removes this file, so git sees it as a change. Restore back to clean
	cd $(CRUNDIR) && git checkout crun.1

# Cleanup to force rebuilding of the crun Makefile in addition to crun
clobber: clean
	-$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C $(CRUNDIR) distclean
	rm -f $(CRUNDIR)/configure

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
