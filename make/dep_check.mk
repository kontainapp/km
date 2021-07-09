#
# Copyright 2021 Kontain Inc.
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
