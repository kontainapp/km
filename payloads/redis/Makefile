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
# Makefile for building and packing Redis payload runtime for KM

TOP := $(shell git rev-parse --show-cdup)
REAL_TOP := $(realpath ${TOP})
CURRENT := ${REAL_TOP}/payloads/redis

all:
	./build.py
	./link-km.sh

clobber:
	rm -rf redis

clean:
	rm -f ${CURRENT}/redis-server.kmd
	cd ${CURRENT}/redis; make clean