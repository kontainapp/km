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

# This list of dependencies was taken from crun's README.md file
DONT_USE_PACKAGES := golang-github-cpuguy83-md2man
DEP_PACKAGES := python git gcc automake autoconf libcap-devel \
		systemd-devel yajl-devel libseccomp-devel \
		glibc-static python3-libmount libtool
# The crun workspace is in the crun directory
CRUNDIR := crun
#SUBDIRS := $(CRUNDIR)

include ${TOP}/make/actions.mk

all:	$(CRUNDIR)/Makefile makefile_ok
	make -C $(CRUNDIR) all

$(CRUNDIR)/Makefile:
	cd $(CRUNDIR); ./autogen.sh; ./configure --disable-systemd

# Run crun's tests
test:	all
	export OCI_RUNTIME=$(CURDIR)/$(CRUNDIR)/crun; \
	make -C $(CRUNDIR) check-TESTS

clean:
	make -C $(CRUNDIR) clean

# Cleanup to force rebuilding of the crun Makefile in addition to crun
clobber:
	make -C $(CRUNDIR) clean distclean
	rm -f $(CRUNDIR)/configure

# Make sure we have all the installed software needed to build crun
.PHONY: swdepcheck
swdepcheck:
	echo "Checking that packages needed for crun build are present"
	@for i in ${DEP_PACKAGES} ; do \
		if ! rpm -qa | grep $$i -q ; then \
			failed=1; echo -e "Missing $$i. To install, run \nsudo dnf install $$i" ; \
		fi ; \
		if [ -n "$$failed" ] ; then false ; fi \
	done

# If they have a Makefile made for some other directory ask them to remake it
# Switching between docker builds and non-docker builds in the same dir can cause this.
.PHONY:	makefile_ok
makefile_ok: $(CRUNDIR)/Makefile
	ls -l $(CRUNDIR)
	if ! grep "abs_builddir = $(CURDIR)" $(CRUNDIR)/Makefile >/dev/null; then \
		echo "The crun Makefile is built to operate in directory:"; \
		grep "abs_builddir =" $(CRUNDIR)/Makefile; \
		echo "rebuild by running make clobber all"; \
                false; \
	else \
		true; \
        fi
