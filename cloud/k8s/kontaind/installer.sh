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
#
# Runs in cluster and expects /opt to be mounted and /dev/kvm available

[ -d /opt ]
[ -e /dev/kvm ]

cp -r /kontain /opt/kontain
# non-privileged containers using kvm device plugin needs access
chmod 666 /dev/kvm

[ -d /opt/kontain ]
[ -x /opt/kontain/bin/km ]
[ -f /opt/kontain/runtime/libc.so ]
