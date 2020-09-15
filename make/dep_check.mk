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

# A target that can be used to verify that needed packages are installed.
# Define DEP_PACKAGES as a list of needed packages and use .check_packages
# as a dependecy in a rule.

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
