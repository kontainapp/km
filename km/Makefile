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

EXEC := km
EXEC_SELINUX_CONTEXT := "system_u:object_r:bin_t:s0"
SOURCES := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c km_mem.c km_mmap.c \
		km_gdb_stub.c gdb_kvm_x86_64.c km_signal.c km_init_guest.c km_intr.c km_coredump.c \
		km_filesys.c km_hc_name.c km_trace.c km_musl_related.c km_decode.c km_proc.c \
		km_guest_asmcode.s km_snapshot.c km_exec.c km_fork.c km_management.c \
		km_kkm.c km_vmdriver.c km_exec_fd_save_recover.c
VERSION_SRC := km_main.c # it has branch/version info, so rebuild it if git info changes
INCLUDES := ${TOP}/include
LLIBS := elf z
COVERAGE := yes
LOCAL_COPTS := -Werror -D_GNU_SOURCE
LOCAL_LDOPTS := -Wl,--script=km_guest_ldcmd

include ${TOP}/make/actions.mk

${BLDDIR}/km_guest_asmcode.o: CFLAGS += -fPIC

# Package KM and libs+tools+docs for release
KM_RELEASE := ${BLDTOP}/kontain.tar.gz
KM_KKM_RELEASE := ${BLDTOP}/kkm.run
KM_BIN_RELEASE := ${BLDTOP}/kontain_bin.tar.gz
KM_BINARIES := km/km container-runtime/krun cloud/k8s/deploy/shim/containerd-shim-krun-v2 kkm.run
# Show this on the release page
RELEASE_MESSAGE ?= Kontain KM Beta Release. branch: ${SRC_BRANCH} sha: ${SRC_SHA}

release: ${KM_RELEASE} ${KM_BIN_RELEASE} ## Package kontain.tar.gz file for release
	ls -lh ${KM_RELEASE} ${KM_BIN}

${KM_RELEASE}: ${KM_BIN} ${TOP}/tools/bin/create_release.sh ## Build a release tar.gz file for KM (called from release: target)
	${TOP}/tools/bin/create_release.sh

${KM_BIN_RELEASE}: ${KM_BIN} ## Build a release tar.gz file for KM runtime binaries
	tar -C ${BLDTOP} -czvf $@ ${KM_BINARIES}

clean-release: ## Clean the release tar files
	rm -f ${KM_RELEASE} ${KM_BIN_RELEASE}

publish-release: ## Publish release with RELEASE_TAG to github
	cd ${TOP}/tools/release; ./release_km.py ${KM_RELEASE} ${KM_BIN_RELEASE} ${KM_KKM_RELEASE} --version ${RELEASE_TAG} --message "${RELEASE_MESSAGE}"
