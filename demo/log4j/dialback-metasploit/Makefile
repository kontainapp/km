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

URL := https://rpm.metasploit.com/metasploit-omnibus/pkg/metasploit-framework-6.1.22%2B20211228112552~1rapid7-1.el6.x86_64.rpm
RPM := $(notdir ${URL})

all: build

run: build
	docker run -it --rm --network=host msfvenom

build: ${RPM}
	docker build -t msfvenom --build-arg RPM=${RPM} .

${RPM}:
	curl --output $@ ${URL}