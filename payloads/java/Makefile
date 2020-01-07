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

all:

fromsrc:
	hg clone  http://hg.openjdk.java.net/jdk/jdk
	cd jdk && bash configure --enable-headless-only  \
		--disable-warnings-as-errors --with-native-debug-symbols=internal \
		--with-jvm-variants=server --with-zlib=bundled
	make -C jdk 

jtreg:
	tar xf jtreg-4.2-b16.tar.gz

clean:
	rm -rf jtreg jdk