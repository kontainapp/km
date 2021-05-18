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
# Build our own custom built version of tensorflow.
# The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
# to disable use of non-POSIX primitives that are not supported on musl.
#
# "make all" builds it locally, takes 2 - 3 hours depending on the machine.
# "make upload" puts the result in azue file share.
# "make download" brings the result from azure share to avoid long compile

TF := tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl

all:
	vagrant up
	@vagrant ssh-config > ssh-config
	scp -F ssh-config default:${TF} .
	@vagrant halt
	@rm ssh-config

upload: ${TF}
	az storage file upload --only-show-errors --share tensorflow --account-name ktf --source ${TF}

download:
	az storage file download --only-show-errors --no-progress --output none --share tensorflow --account-name ktf --path ${TF}
