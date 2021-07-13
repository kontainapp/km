#!/bin/bash
#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#

CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags '-T 0x201000 -w -extldflags "-no-pie -static -Wl,--gc-sections"' -o server.km server.go
