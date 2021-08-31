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
# Build tensorflow inference app in vagrant VM or container

BUILD := ../../tools/hashicorp/build_tensorflow
TF := tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl

vm: ${BUILD}/${TF}
	vagrant up
	@echo "Run 'vagrant ssh' to login into the VM"

container: ${BUILD}/${TF}
	@-docker rmi -f test-app
	cp ${BUILD}/${TF} .
	docker build -t test-app .
	@rm ${TF}
	@echo "Run 'docker run --runtime=krun --name test-app --rm -it -p 5000:5000 test-app /bin/sh' to login into container"