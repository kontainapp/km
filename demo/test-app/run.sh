#!/bin/sh
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

rm -f /tmp/km.sock
cd /tmp
echo > /mnt/start_time
KM_MGTPIPE=/tmp/km.sock python /home/appuser/app.py
