#!/bin/sh
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
# 
# Install kontain related artifacts and do verification.

rm -rf /opt/kontain
cp -r /kontain /opt/kontain
chmod 666 /dev/kvm

[ -e /dev/kvm ]
[ -d /opt/kontain ]
[ -f /opt/kontain/bin/km ]
[ -f /opt/kontain/runtime/libc.so ]
