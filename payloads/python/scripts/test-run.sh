#!/usr/bin/bash -ex
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
