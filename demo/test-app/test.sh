#!/bin/bash
#
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
# This is a minimal script used to get time to active readings.
#
set -e
[ "$TRACE" ] && set -x

CONTAINER=test-app

until $(curl --output /tmp/out.json --silent --fail -X POST -F image=@dog2.jpg 'http://localhost:5000/predict'); do
   sleep 0.001
done

end=$(date +%s%N)
start=$(date +%s%N -d $(ls --full-time tmp/start_time | awk -e '{print $7}'))
dur=$(expr $end - $start)
echo "Response time $(expr $dur / 1000000000).$(printf "%.03d" $(expr $dur % 1000000000 / 1000000)) secs"
jq . /tmp/out.json
echo ""