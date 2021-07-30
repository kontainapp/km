#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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

