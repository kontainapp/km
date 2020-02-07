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

JDK_VERSION=jdk-11.0.6+10

all:

fromsrc: jtreg
	git clone https://github.com/openjdk/jdk11u.git ${JDK_VERSION} -b ${JDK_VERSION}
	cd ${JDK_VERSION} && bash configure \
		--disable-warnings-as-errors --with-native-debug-symbols=internal \
		--with-jvm-variants=server --with-zlib=bundled --with-jtreg=$(shell pwd)/jtreg \
		--enable-jtreg-failure-handler
	make -C ${JDK_VERSION} images
	./link_km.sh

link:
	./link_km.sh

jtreg:
	tar xf jtreg-4.2-b16.tar.gz

clean:
	rm -rf jtreg ${JDK_VERSION}
