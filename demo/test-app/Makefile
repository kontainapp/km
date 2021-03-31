# Copyright © 2021 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Build tensorflow inference app in vagrant VM or container

BUILD := ../../tools/hashicorp/build_tf
TF := tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl

vm: ${BUILD}/${TF}
	vagrant up
	@echo "Run 'vagrant ssh' to login into the VM"

container: ${BUILD}/${TF}
	cp ${BUILD}/${TF} .
	docker build -t test-app .
	@rm ${TF}
	@echo "Run 'docker run --device /dev/kkm --name test-app --rm -it -p 5000:5000 test-app bash' to login into container"