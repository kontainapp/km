#!/usr/bin/bash -e
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
#
# Wrapper/entrypoint for running tests.
#   test-run.sh PYTHON
#   where PYTHON could be path to python->km symlink, or "km_path python.km_path"
#
KM_BIN=${1:-Please_pass_km_path}
PYTHON=${2:-Please_pass_python_interpreter}

${KM_BIN} ${PYTHON} ./scripts/sanity_test.py
${KM_BIN} ${PYTHON} ./test_unittest.py
${KM_BIN} ${PYTHON} ./cpython/Lib/unittest/test/
rm -f kmsnap
${KM_BIN} ${PYTHON} ./test_snapshot.py
[ ! -f kmsnap ] && echo No test_snapshot
rm -f kmsnap
