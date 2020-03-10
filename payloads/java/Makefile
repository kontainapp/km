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
# Makefile for building and packing Python payload runtime for KM
TOP := $(shell git rev-parse --show-toplevel)
COMPONENT := java-11

JDK_VERSION := jdk-11.0.6+10
DEP_PACKAGES := java-11-openjdk java-11-openjdk-devel

all: link

fromsrc: .check ${JDK_VERSION} jtreg ${KM_BIN} ${KM_LDSO}
	cd ${JDK_VERSION} && bash configure \
		--disable-warnings-as-errors --with-native-debug-symbols=internal \
		--with-jvm-variants=server --with-zlib=bundled --with-jtreg=$(realpath jtreg) \
		--enable-jtreg-failure-handler
	make -C ${JDK_VERSION} images
	./link_km.sh

.PHONY: .check
.check:
	@for i in ${DEP_PACKAGES} ; do \
		if ! rpm -qa | grep $$i -q ; then \
			failed=1; echo -e "Missing $$i. To install, run \nsudo dnf install $$i" ; \
		fi ; \
		if [ -n "$$failed" ] ; then false ; fi \
	done

link:
	./link_km.sh

${JDK_VERSION}:
	git clone https://github.com/openjdk/jdk11u.git ${JDK_VERSION} -b ${JDK_VERSION}

jtreg:
	tar xf jtreg-4.2-b16.tar.gz

clobber:
	rm -rf jtreg ${JDK_VERSION}

clean:
	make -C ${JDK_VERSION} clean

# TODO: Fix the runenv-image for java.
export define runenv_prep
	cp ${KM_BIN} ${PAYLOAD_KM} Hello.java ${RUNENV_PATH}
endef

include ${TOP}/make/images.mk
