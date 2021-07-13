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

if [ ! -d meltdown ] ; then 
  git clone https://github.com/IAIK/meltdown
  (cd meltdown; patch -p1 < ../meltdown.patch)
fi
make -C meltdown
