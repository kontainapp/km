#!/usr/bin/bash -ex
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
# Wrapper/entrypoint for running tests.
#   test-run.sh PYTHON
#   where PYTHON could be path to python->km symlink, or "km_path python.km_path"
#
PYTHON=${1:-Please_pass_python_interpreter}

${PYTHON} ./scripts/sanity_test.py
${PYTHON} ./test_unittest.py
${PYTHON} ./cpython/Lib/unittest/test/
rm -f kmsnap
${PYTHON} ./test_snapshot.py
[ ! -f kmsnap ] && echo No test_snapshot
rm -f kmsnap
