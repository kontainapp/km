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


#  Temp makefile to build GO webserver before it's fully integrated with .mk files and CI
# 'make help' for help

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/locations.mk

all build: server   

server: goweb/server.go ## builds go executable
	CGO_ENABLED=0 go build goweb/server.go

PORT ?= 15213
ENDPOINT ?= localhost:${PORT}
WORDCOUNT ?= 10
VERBOSE ?= false
FUNCTION ?= mmap

test-start-v: server ## does `test-start` with `--km-log-to=km_logs.log`
	${KM_BIN} --km-log-to=km_logs.log server -port=${PORT} -v=${VERBOSE} -f=${FUNCTION}
	sleep 0.5s

test-start: server ##  starts the server and listens on `PORT`
	${KM_BIN} server -port=${PORT} -v=${VERBOSE} -f=${FUNCTION}
	sleep 0.5s

test: server ##  performs a single request and stops the server
	${KM_BIN} server -port=${PORT} -v=${VERBOSE} -f=${FUNCTION} &
	sleep 0.5s
	curl -X GET ${ENDPOINT}/simpleIO\?wordCount\=${WORDCOUNT}
	curl --http0.9 ${ENDPOINT}/stop
	 
test-long: server ## performs 300000 sequential requests and stops the
	${KM_BIN} server -port=${PORT} -v=${VERBOSE} -f=${FUNCTION} &
	sleep 0.5s
	for i in $$(seq 300000) ; do curl -X GET ${ENDPOINT}/simpleIO\?wordCount\=${WORDCOUNT}; done
	curl --http0.9 ${ENDPOINT}/stop

force-stop: server ## stops the server
	curl --http0.9 ${ENDPOINT}/stop

clean: ## removes executible
	rm server

.PHONY: all build clean test test-start-v test-start test-long force-stop 
