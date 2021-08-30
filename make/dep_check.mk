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
#

# A target that can be used to verify that needed packages are installed.
# Define DEP_PACKAGES as a list of needed packages and use .check_packages
# as a dependecy in a rule.
# TODO: fix payloads/java/Makefile and payloads/nginx/Makefile to use this.

export define check_packages 
	echo "Checking that packages ${DEP_PACKAGES} are present"
	for i in ${DEP_PACKAGES} ; do \
		if ! rpm -qa | grep $$i -q ; then \
			failed=1; \
			echo -e "Package $$i is missing. To install, run \nsudo dnf install $$i"; \
		fi; \
	done;
	if [ -n "$$failed" ] ; then \
		false; \
	fi
endef

.check_packages:
	eval $(check_packages)

PODMAN_PATH := /usr/bin/podman
CONTAINER_CONF := /usr/share/containers/containers.conf 
HOME_CONTAINER_CONF := ~/.config/containers/containers.conf
KM_OPT_KRUN := /opt/kontain/bin/krun
CONTAINER_INIT_PATH := /usr/libexec/docker/docker-init
SESEARCH_PATH := /usr/bin/sesearch

# To use podman certain things must exist on the system running kontainers with podman.
# Check for these prerequisites with this target.
# /opt/kontain/bin/km and /opt/kontain/bin/krun must exist
# /opt/kontain/bin/km must have the proper selinux context
# Changes to the selinux policy are needed to allow krun to access /dev/kvm and probably /dev/kkm
# podman must be installed
# podman must be configured to allow it to use krun.
.check_podman_prereqs:
	@if [ ! -e ${KM_OPT_KM} ]; then \
		echo "${KM_OPT_KM} is missing, run 'make -C km' to build it"; \
		false; \
	fi
	@if [ ! -e ${KM_OPT_KRUN} ] ; then \
		echo "${KM_OPT_KM} is missing, run 'make -C container-runtime' to build it"; \
		false; \
	fi
	@if [ ! -e ${PODMAN_PATH} ]; then \
		echo "${PODMAN_PATH} is missing, run 'sudo dnf install podman' to have it installed"; \
		false; \
	fi
	@if ! grep -q 'krun = \[' ${CONTAINER_CONF} ${HOME_CONTAINER_CONF}; then \
		echo "podman is not configured to allow krun use"; \
		echo "Add the following to ${CONTAINER_CONF} or ${HOME_CONTAINER_CONF} in the [engine.runtimes] table"; \
		echo "krun = ["; \
		echo "        \"/opt/kontain/bin/krun\","; \
		echo "]"; \
		false; \
	fi
	@if ! grep -q "init_path = \"${CONTAINER_INIT_PATH}\"" ${CONTAINER_CONF} ${HOME_CONTAINER_CONF}; then \
		echo "podman is not configured to use docker's init program"; \
		echo "This may cause problems when running 'podman run --init ....'"; \
	fi
	@if ! getenforce | grep -q Enforcing; then \
		echo "selinux not configured or not enforcing"; \
	fi
	@if ! ls -Z ${KM_OPT_KM} | grep -q -e system_u:object_r:bin_t -e system_u:object_r:container_file_t; then \
		echo "${KM_OPT_KM} has the wrong selinux context"; \
		echo "run 'chcon system_u:object_r:bin_t:s0 ${KM_OPT_KM}' to repair"; \
		false; \
	fi
	@if [ ! -e ${SESEARCH_PATH} ]; then \
		echo "sesearch not installed, run 'dnf install setools-console'"; \
		false; \
	fi
	@echo "Running sesearch, this takes a few seconds"; \
	if ! ${SESEARCH_PATH} --allow | grep -q "allow container_t kvm_device_t:chr_file"; then \
		echo "selinux policy will not allow podman et.al. to run krun"; \
		false; \
	fi
	@echo "podman should be able to run kontain kontainers"
