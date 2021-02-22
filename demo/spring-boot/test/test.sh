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
# This is a minimal script used to get time to active readings.
#
set -e
[ "$TRACE" ] && set -x

CONTAINER=KM_SpringBoot_Demo

until $(curl --output /dev/null --silent --fail http://localhost:8080/greeting); do
   sleep 0.001
done

end=$(date +%s%N)
docker cp $CONTAINER:/tmp/start_time /tmp/start_time
dur=$(expr $end - $(cat /tmp/start_time))
echo "Response time $(expr $dur / 1000000000).$(printf "%.03d" $(expr $dur % 1000000000 / 1000000)) secs"
curl http://localhost:8080/greeting
echo ""
