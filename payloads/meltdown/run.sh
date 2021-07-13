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

TMPNAME="/tmp/xxx_"$$

trap "{ sudo pkill secret; rm -f $TMPNAME; exit; }" SIGINT

sudo taskset 0x4 ./meltdown/secret | tee $TMPNAME &
sleep 1
taskset 0x1 ./meltdown/physical_reader `awk '/Physical address of secret:/{print $6}' $TMPNAME` 0xffff888000000000
