# Copyright © 2021 Kontain Inc. All rights reserved.

# Kontain Inc CONFIDENTIAL

#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

#  Temp makefile to build GO webserver before it's fully integrated with .mk files and CI
# 'make help' for help

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/locations.mk

build: ## builds go executable
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
