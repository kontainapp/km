#!/usr/bin/bash
# Run tests
bats/bin/bats -t km_core_tests.bats
exit_code=$?
# Print time info
echo '------------------------------------------------------------------------------'
echo "Tests slower than 0.1 sec:"
grep elapsed $TIME_INFO | grep -v "elapsed 0:00.[01]" | sort -r
echo '------------------------------------------------------------------------------'
echo ""
exit $exit_code
