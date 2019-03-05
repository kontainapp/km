#!./bats/bin/bats
#
# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.
#
# BATS (BASH Test Suite) definition for KM core test pass
#
# See ./bats/... for docs
#

# we will kill any test if takes longer
timeout=60s

# KM binary location.
KM_BIN=../build/km/km
# this is how we invoke KM - with a timeout
KM="timeout -v --foreground $timeout ${KM_BIN}"

# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

# Teardown for each test. Note that printing to stdout/stderr in this function
# only shows up on errors. For print on success too, redirect to >&3
teardown() {
      echo -e "${output}"
}

# Now the actual tests.
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_core_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "basic vm setup, workload invocation and exit value check" {
   run $KM exit_value_test.km
   [ $status -eq 17 ]
}

@test "load elf and layout check" {
   run $KM load_test.km
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   [ $status -eq 0 ]
}

@test "KVM memslot / phys mem sizes" {
   run ./memslot_test
   [ $status -eq 0  ]
}

@test "brk() call" {
   run $KM brk_test.km
   [ $status -eq 0 ]
}

@test "basic run and print(hello world)" {
   run ./hello_test "$BATS_TEST_DESCRIPTION"
   [ $status -eq 0 ]
   linux_out="${output}"

   run $KM hello_test.km "${BATS_TEST_DESCRIPTION}"
   [ $status -eq 0 ]
   # argv[0] differs for linux and km so strip it out , and then compare results
   diff <(echo -e "$linux_out" | fgrep -v 'argv[0]')  <(echo -e "$output" | fgrep -v 'argv[0]')
}

@test "basic HTTP/socket I/O (hello_html)" {
   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   run curl -s $address
   [ $status -eq 0 ]
   linux_out="${output}"

   ($KM hello_html_test.km &)
	run curl -s $address
   [ $status -eq 0 ]
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mmap/munmap" {
   expected_status=0
   # we expect 3 ENOMEM failures on 36 bit buses
   bus_width=$(${KM} -V exit_value_test.km 2>& 1 | awk '/physical memory width/ {print $6;}')
   if [ $bus_width -eq 36 ] ; then expected_status=3 ; fi

   run $KM mmap_test.km
   [ $status -eq $expected_status ]
}

@test "futex example" {
   skip "TODO: convert to test"

   run $KM futex.km
   [ "$status" -eq 0 ]
}

@test "gdb support" {
   $KM -g 3333 gdb_test.km  &
	sleep 0.5
	run gdb -q -nx --ex="target remote :3333"  --ex="source cmd_for_test.gdb"  \
         --ex=c --ex=q gdb_test.km
   [ $(echo "$output" | grep -cw 'SUCCESS') == 1 ]
}

@test "basic threads loop" {
   skip "TODO: convert to test"

   run $KM hello_t_loops_test.km
   [ "$status" -eq 0 ]
}

@test "pthread_create and mutex" {
   run $KM mutex_test.km
   [ $status -eq  0 ]
}