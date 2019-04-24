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

bus_width() {
   #  use KM to print out physical memory width on the test machine
   echo $(${KM} -V exit_value_test.km 2>& 1 | awk '/physical memory width/ {print $6;}')
}

# Teardown for each test. Note that printing to stdout/stderr in this function
# only shows up on errors. For print on success too, redirect to >&3
teardown() {
      echo -e "\nkm output:\n${output}"
}

# Now the actual tests.
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_core_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "setup_basic: basic vm setup, workload invocation and exit value check (exit_value_test)" {
   run $KM exit_value_test.km
   [ $status -eq 17 ]
}

@test "setup_load: load elf and layout check (load_test)" {
   run $KM load_test.km
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   [ $status -eq 0 ]
}

@test "hc_check: invoke wrong hypercall (hc_test)" {
   run $KM hc_test.km 400
   [ $(echo -e "$output" | grep -cw "unexpected hypercall 400") == 1 ]

   run $KM hc_test.km -10
   [ $(echo -e "$output" | grep -cw "unexpected IO port activity, port 0x7ff6 0x4 bytes out") == 1 ]

   run $KM hc_test.km 1000
   [ $(echo -e "$output" | grep -cw "unexpected IO port activity, port 0x83e8 0x4 bytes out") == 1 ]
}

@test "km_main: wait on signal (hello_test)" {
   ($KM -w hello_test.km &)
   kill -SIGUSR1 `pidof km`
}

@test "mem_slots: KVM memslot / phys mem sizes (memslot_test)" {
   run ./memslot_test
   [ $status -eq 0 ]
}

@test "mem_brk: brk() call (brk_test)" {
   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   sudo sysctl -w vm.overcommit_memory=1
   run $KM brk_test.km
   [ $status -eq $expected_status ]
   sudo sysctl -w vm.overcommit_memory=0
}

@test "hc_basic: basic run and print hello world (hello_test)" {
   args="more_flags to_check: -f and check --args !"
   run ./hello_test $args
   [ $status -eq 0 ]
   linux_out="${output}"

   run $KM hello_test.km $args
   [ $status -eq 0 ]
   # argv[0] differs for linux and km so strip it out, and then compare results
   diff <(echo -e "$linux_out" | fgrep -v 'argv[0]') <(echo -e "$output" | fgrep -v 'argv[0]' | fgrep -v 'pdpe1g')
}

@test "hc_socket: basic HTTP/socket I/O (hello_html_test)" {
   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   sleep 0.5s
   run curl -s $address
   [ $status -eq 0 ]
   linux_out="${output}"

   ($KM hello_html_test.km &)
   sleep 0.5s
	run curl -s $address
   [ $status -eq 0 ]
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mem_mmap0: mmap and munmap with addr=0 (mmap_test)" {
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run $KM mmap_test.km
   [ $status -eq $expected_status ]
}

@test "futex example" {
   skip "TODO: convert to test"

   run $KM futex.km
   [ "$status" -eq 0 ]
}

@test "gdb_basic: gdb support (gdb_test)" {
   gdb_port=3333
   $KM -g$gdb_port gdb_test.km &
	sleep 0.5
	run gdb -q -nx --ex="target remote :$gdb_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test.km
   [ $(echo "$output" | grep -cw 'SUCCESS') == 1 ]
}

@test "threads_basic: basic threads create, exit and join (hello_2_loops_test)" {
   run $KM hello_2_loops_test.km
   [ "$status" -eq 0 ]
}

@test "threads_exit_grp: force exit when threads are in flight (exit_grp_test)" {
   run $KM exit_grp_test.km
   # the test can exit(17) from main thread or random exit(11) from subthread
   [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex: mutex (mutex_test)" {
   run $KM mutex_test.km
   [ $status -eq 0 ]
}

@test "mem_test: threads create, malloc/free, exit and join (mem_test)" {
   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   sudo sysctl -w vm.overcommit_memory=1
   run $KM mem_test.km
   [ "$status" -eq $expected_status ]
   sudo sysctl -w vm.overcommit_memory=0
}

@test "pmem_test: test physical memory override (pmem_test)" {
   # Don't support bus smaller than 32 bits
   run $KM -P 31 hello_test.km
   [ "$status" -ne 0 ]
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run $KM -P 33 hello_test.km
      [ "$status" -eq 0 ]
   fi
   # Don't support guest bus larger the host bus.
   run $KM -P `expr $(bus_width) + 1` hello_test.km
   [ "$status" -ne 0 ]
   # Don't support 0 width bus
   run $KM -P 0 hello_test.km
   [ "$status" -ne 0 ]
   run $KM -P -1 hello_test.km
   [ "$status" -ne 0 ]
}

@test "brk_map_test: test brk and map w/physical memory override (brk_map_test)" {
   run $KM -P 33 brk_map_test.km -- 33
   [ "$status" -eq 0 ]
}