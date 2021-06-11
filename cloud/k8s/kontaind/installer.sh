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
#
# to sanity check offline:
# docker run --device /dev/kvm -v /tmp/x:/opt:z --rm -it  kontainapp/runenv-kontain-installer

set -ex

device=/dev/kvm

[ -d /opt ] || (echo Missing /opt dir ; false)
[ -e $device ] || (echo Missing access to $device  ; false)

mkdir -p /opt/kontain
rm -rf /opt/kontain/*
cp -r /kontain/* /opt/kontain/
# non-privileged containers using kvm device plugin needs access
chmod 666 /dev/kvm

echo Validating files presense...
[ -x /opt/kontain/bin/km ] || (echo Missing KM ; false)
[ -f /opt/kontain/runtime/libc.so ] || (echo Missing libc.so ; false)

echo Installed KM version:
/opt/kontain/bin/km -v 2>& 1
