#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk

ME_SRCDIR := ${TOP}/lib/mimalloc
ME_BLDDIR := ${BLDTOP}/lib/mimalloc
ME_MI_SRCDIR := ${ME_SRCDIR}/mimalloc
ME_MI_BLDDIR := ${ME_BLDDIR}/mimalloc

all:
	cmake -S ${ME_MI_SRCDIR} -B ${ME_MI_BLDDIR} -D MI_BUILD_TESTS:BOOL=OFF -D CMAKE_BUILD_TYPE=Release -D CMAKE_SHARED_LINKER_FLAGS="-L/opt/kontain/alpine-lib/usr/lib -L/opt/kontain/alpine-lib/lib -Wl,-rpath,/opt/kontain/alpine-lib/usr/lib:/opt/kontain/alpine-lib/lib:"
	make -C ${ME_MI_BLDDIR}
	[ -d ${KM_OPT_LIB} ] || mkdir ${KM_OPT_LIB}
	cp ${ME_MI_BLDDIR}/libmimalloc.a ${KM_OPT_LIB}/libmimalloc.a
	chmod 0644 ${KM_OPT_LIB}/libmimalloc.a
	cp ${ME_MI_BLDDIR}/libmimalloc.so.1.7 ${KM_OPT_LIB}/libmimalloc.so.1.7
	chmod 0755 ${KM_OPT_LIB}/libmimalloc.so.1.7
	ln -f -s ${KM_OPT_LIB}/libmimalloc.so.1.7  ${KM_OPT_LIB}/libmimalloc.so
	ldd ${ME_MI_BLDDIR}/libmimalloc.so.1.7

