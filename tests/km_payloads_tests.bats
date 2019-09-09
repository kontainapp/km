# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.
#

load test_helper

# Now the actual tests.
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_payloads_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "busybox: check busybox payload (hello_test)" {
    run km_with_timeout busybox/busybox/busybox.km echo "Hello"
    assert_success
    assert_output --partial "Hello"
}

@test "node: check nodejs payload (hello_test)" {
    run km_with_timeout  node/node/out/Release/node.km -e 'console.log("Hello")'
    assert_success
    assert_output --partial "Hello"
}

@test "python: check python payload (hello_test)" {
    run km_with_timeout python/cpython/python.km python/scripts/hello_again.py
    assert_success
    assert_output --partial "Hello again"
}

