#!/bin/bash
# use this if started as a standalone script:
if [ -z "$BATS_TEST_FILENAME" ] ; then "exec" "`dirname $0`/bats/bin/bats" "$0" "$@" ; fi

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

# bats sits under tests, so this will move us to tests
cd $BATS_ROOT/..

# KM binary location.
if [ -z "$KM_BIN" ] ; then
   KM_BIN="$(git rev-parse --show-toplevel)/build/km/km"
   echo $KM_BIN >&3
fi

# we will kill any test if takes longer
timeout=60s
# this is how we invoke KM - with a timeout
KM="timeout -v --foreground $timeout ${KM_BIN}"

# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

# Teardown for each test. Note that printing to stdout/stderr in this function
# only shows up on errors. For print on success too, redirect to >&3
teardown() {
      echo -e "\nkm output:\n${output}"
}

# Now the actual tests.
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_core_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "setup_basic: basic vm setup, workload invocation and exit value check" {
   run $KM exit_value_test.km
   [ $status -eq 17 ]
}

@test "setup_load: load elf and layout check" {
   run $KM load_test.km
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   [ $status -eq 0 ]
}

@test "hypercall: invoke wrong hypercall" {
   run $KM hc_test.km 400
   echo -e "$output" | grep -q "unexpected hypercall 400"
   [ $status -eq 1 ]

   run $KM hc_test.km -10
   echo -e "$output" | grep -q "unexpected IO port activity, port 0x7ff6 0x4 bytes out"
   [ $status -eq 1 ]   

   run $KM hc_test.km 1000
   echo -e "$output" | grep -q "unexpected IO port activity, port 0x83e8 0x4 bytes out"
   [ $status -eq 1 ]   
}

@test "wait on signal" {
   ($KM -w hello_test.km &)
   kill -SIGUSR1 `pidof km`
}

@test "mem_basic: KVM memslot / phys mem sizes" {
   run ./memslot_test
   [ $status -eq 0 ]
}

@test "mem_brk: brk() call" {
   run $KM brk_test.km
   [ $status -eq 0 ]
}

@test "hc_basic: basic run and print hello world" {
   run ./hello_test "$BATS_TEST_DESCRIPTION" -f
   [ $status -eq 0 ]
   linux_out="${output}"

   run $KM hello_test.km "${BATS_TEST_DESCRIPTION}" -f
   [ $status -eq 0 ]
   # argv[0] differs for linux and km so strip it out , and then compare results
   diff <(echo -e "$linux_out" | fgrep -v 'argv[0]') <(echo -e "$output" | fgrep -v 'argv[0]')
}

@test "hc_socket: basic HTTP/socket I/O (hello_html)" {
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

@test "mem_mmap0: mmap and munmap with addr=0" {
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

@test "gdb_basic: gdb support" {
   $KM -g 3333 gdb_test.km  &
	sleep 0.5
	run gdb -q -nx --ex="target remote :3333"  --ex="source cmd_for_test.gdb"  \
         --ex=c --ex=q gdb_test.km
   [ $(echo "$output" | grep -cw 'SUCCESS') == 1 ]
}

@test "threads_basic: basic threads create, exit and join" {
   run $KM hello_2_loops_test.km
   [ "$status" -eq 0 ]
}

@test "threads_mutex: mutex" {
   run $KM mutex_test.km
   [ $status -eq  0 ]
}
