#!/bin/bash
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
# Basic test of extensions. Just making sure it does not fail
#
set -e ; [ "$TRACE" ] && set -x
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
./extensions/analyze_modules.sh "markupsafe falcon gevent"

