#!/bin/sh -e
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Install kontain related artifacts and do verification.
#
# Runs in cluster and expects /opt to be mounted and /dev/kvm available
#
# to sanity check offline:
# docker run --device /dev/kvm -v /tmp/x:/opt:z --rm -it  kontainapp/runenv-kontain-installer

set -x

device=/dev/kvm

[ -d /opt ] || (echo Missing /opt dir ; false)
[ -e $device ] || (echo Missing access to $device  ; false)

mkdir -p /opt/kontain
rm -rf /opt/kontain/bin
cp -r /kontain/bin /opt/kontain/bin/
# non-privileged containers using kvm device plugin needs access
chmod 666 /dev/kvm

echo Validating files presense...
[ -x /opt/kontain/bin/km ] || (echo Missing KM ; false)

echo Installed KM version:
/opt/kontain/bin/km -v 2>& 1
