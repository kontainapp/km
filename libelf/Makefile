# Copyright © 2021 Kontain Inc. All rights reserved.
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

# Makefile for building static libelf fron elfutils
# Since we are using elfutils generated Makefile, this Makefile tries to adapt kontain's targets
# in actions.mk to the targets in the elfutils Makefile.
# Need to run autoreconf and configure first time after git clone
# "make all" builds libelf.a and a whole bunch of other stuff

DEP_PACKAGES := flex bison zstd gettext-devel bsdtar xz-devel
SRCDIR := elfutils

include ${TOP}/make/actions.mk

all:	$(SRCDIR)/Makefile .makefile_check
	make -C $(SRCDIR) -j

$(SRCDIR)/Makefile:
	cd $(SRCDIR); autoreconf -i -f && \
		./configure --enable-maintainer-mode --disable-libdebuginfod --disable-debuginfod && \
		rm config/profile.sh config/profile.csh

clean::
	-make -C $(SRCDIR) clean

# Cleanup to force rebuilding of the Makefile
clobber:
	-make -C $(SRCDIR) distclean

# If they have a Makefile made for some other directory ask them to remake it
# Switching between docker builds and non-docker builds in the same dir can cause this.
.PHONY:	.makefile_check
.makefile_check: $(SRCDIR)/Makefile
	@if ! grep -q "abs_builddir = $()" $(SRCDIR)/Makefile ; then \
		echo -e "${RED}The generated crun/Makefile uses dir different from the current one ($(CURDIR))${NOCOLOR}"; \
		echo -e "${RED}Run 'make clobber' and then re-make${NOCOLOR}"; \
		false; \
	fi

include ${TOP}/make/dep_check.mk
