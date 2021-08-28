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

.PHONY: all
all: bin/faktory

.PHONY: release
release: all

bin/faktory:
	go build -o bin/faktory ./cmd/faktory/...

.PHONY: clean
clean:
	rm -rf bin

.PHONY: test
test: bin/faktory
	@# sudo is required to run tests
	sudo -E env "GOPATH=${GOPATH}" env "GOBIN=${GOBIN}" go test -p 1 -count=1 ./tests/...

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'
