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
#
# Switch python between km and native

cd /usr/bin
rm -f python3.8

if [ "$1" == "km" ] ; then
   ln -s /opt/kontain/bin/km python3.8
else
   ln -s python3.8.orig python3.8
fi
