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
