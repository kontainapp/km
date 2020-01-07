#!/bin/bash
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Basic test of extensions. Just making sure it does not fail
#
set -e
[ "$TRACE" ] && set -x
# we assume the script is in python/extensions, this puts us in payloads/python
cd "$( dirname "${BASH_SOURCE[0]}")/.."

make clobber
make
make build-modules pack-modules push-modules MODULES=markupsafe
make clean
make build-modules
make custom
make custom CUSTOM_NAME=numpy
make pack-modules push-modules
make clean
make pull-modules
make custom
./extensions/analyze_modules "markupsafe falcon gevent"

