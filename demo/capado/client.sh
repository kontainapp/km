#!/bin/bash
#
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

# port: localhost:9090 sprintboot, 172.17.0.4:9091 micronaut

#until $(curl --output /dev/null --silent --fail http://172.17.0.4:9091/hello); do
until $(curl --output /dev/null --silent --fail http://localhost:9091/hello); do
    sleep 0.01
done
echo $(date +%s%N)

