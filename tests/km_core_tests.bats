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

if [ -z "$TIME_INFO" ] ; then
   echo "Please make sure TIME_INFO env is defined. We will put detailed timing info there">&3
   exit 10
fi

# we will kill any test if takes longer
timeout=60s

# this is how we invoke KM - with a timeout
function km_with_timeout () {
   /usr/bin/time -f="elapsed %E user %U system %S mem %M KiB (km $*) " \
      -a -o $TIME_INFO timeout --foreground $timeout ${KM_BIN} $*
}

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

@test "setup_link: check if linking produced text segment where we expect" {
   run objdump -wp load_test.km
   [[ $(echo -e "$output" | awk '/LOAD/{print $5}' | sort -g | head -1) -eq 0x200000 ]]
}

@test "setup_basic: basic vm setup, workload invocation and exit value check (exit_value_test)" {
   for i in $(seq 1 200) ; do # a loop to catch race with return value, if any
      run km_with_timeout exit_value_test.km
      [ $status -eq 17 ]
   done
}

@test "setup_load: load elf and layout check (load_test)" {
   run km_with_timeout load_test.km
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   [ $status -eq 0 ]
}

@test "hc_check: invoke wrong hypercall (hc_test)" {
   run km_with_timeout hc_test.km 400
   [ $status == 31 ]  #SIGSYS
   [ $(echo -e "$output" | grep -F -cw "Bad system call") == 1 ]

   run km_with_timeout hc_test.km -- -10
   [ $status == 31 ]  #SIGSYS
   [ $(echo -e "$output" | grep -F -cw "Bad system call") == 1 ]

   run km_with_timeout hc_test.km 1000
   [ $status == 31 ]  #SIGSYS
   [ $(echo -e "$output" | grep -F -cw "Bad system call") == 1 ]

   run km_with_timeout hc_test.km --bad-arg 3
   [ $status == 31 ]  #SIGSYS
   [ $(echo -e "$output" | grep -F -cw "Bad system call") == 1 ]
}

@test "km_main: wait on signal (hello_test)" {
   run timeout -s SIGUSR1 1s ${KM_BIN} --wait-for-signal hello_test.km
   [ $status -eq 124 ]
}

@test "km_main: optargs (hello_test)" {
   # -v flag prints version and branch
   run ${KM_BIN} -v  hello_test.km
   [ $status -eq 0 ]
   branch=$(git rev-parse --abbrev-ref  HEAD)
   [ $(echo -e "$output" | grep -F -cw "$branch") == 1 ]

   run ${KM_BIN} -Vkvm  hello_test.km
   # -V<regex> turns on tracing for a subsystem. Check it for kvm
   [ $status -eq 0 ]
   [ $(echo -e "$output" | grep -F -cw "KVM_EXIT_IO") > 1 ]

   # -g[port] turns on gdb and tested in gdb coverage. Let's validate a failure case
   run ${KM_BIN} -gfoobar  hello_test.km
   [ $status -eq 1 ]
   [ $(echo -e "$output" | grep -F -cw "Wrong gdb port number") == 1 ]

   # -C sets coredump file name
   corefile=/tmp/km$$
   run ${KM_BIN} -Vcoredump -C $corefile hello_test.km
   [ $status -eq 0 ]
   [ $(echo -e "$output" | grep -F -cw "Setting coredump path to $corefile") == 1 ]

   # -P sets Physical memory bus width
   run ${KM_BIN} -P 31   hello_test.km
   [ $status -eq 1 ]
   [ $(echo -e "$output" | grep -F -cw "Guest memory bus width must be between 32 and 63") == 1 ]

   run ${KM_BIN} -P32 hello_test.km
   [ $status -eq 0 ]

   # invalid option
   run ${KM_BIN} -X
   [ $status -eq 1 ]
      [ $(echo -e "$output" | grep -F -cw "invalid option") == 1 ]

}

@test "mem_slots: KVM memslot / phys mem sizes (memslot_test)" {
   run ./memslot_test
   [ $status -eq 0 ]
}

@test "mem_brk: brk() call (brk_test)" {
   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   run km_with_timeout --overcommit-memory brk_test.km
   [ $status -eq $expected_status ]
}

@test "hc_basic: basic run and print hello world (hello_test)" {
   args="more_flags to_check: -f and check --args !"
   run ./hello_test $args
   [ $status -eq 0 ]
   linux_out="${output}"

   run km_with_timeout hello_test.km $args
   [ $status -eq 0 ]
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "hc_socket: basic HTTP/socket I/O (hello_html_test)" {
   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   sleep 0.5s
   run curl -s $address
   [ $status -eq 0 ]
   linux_out="${output}"

   (km_with_timeout hello_html_test.km &)
   sleep 0.5s
	run curl -s $address
   [ $status -eq 0 ]
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mem_mmap0: mmap and munmap with addr=0 (mmap_test)" {
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run km_with_timeout mmap_test.km
   [ $status -eq $expected_status ]
}

@test "futex example" {
   skip "TODO: convert to test"

   run km_with_timeout futex.km
   [ "$status" -eq 0 ]
}

@test "gdb_basic: gdb support (gdb_test)" {
   km_gdb_default_port=3333
   # start KM in background, give it time to start, and connect with gdb cliennt
   km_with_timeout -g gdb_test.km &
   gdb_pid=`jobs -p` ; sleep 0.5
	run gdb -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test.km
   # check that gdb found what it is supposed to find
   [ $(echo "$output" | grep -F -cw 'SUCCESS') == 1 ]
   # check that KM exited normally
   wait $gdb_pid
   [ $status == 0 ]
}

@test "threads_basic: basic threads create, TSD, exit and join (hello_2_loops_test)" {
   run km_with_timeout hello_2_loops_test.km
   [ "$status" -eq 0 ]
}

@test "threads_basic: threads with TLS, create, exit and join (hello_2_loops_tls_test)" {
   run km_with_timeout hello_2_loops_tls_test.km
   [ "$status" -eq 0 ]
}

@test "threads_exit_grp: force exit when threads are in flight (exit_grp_test)" {
   run km_with_timeout exit_grp_test.km
   # the test can exit(17) from main thread or random exit(11) from subthread
   [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex: mutex (mutex_test)" {
   run km_with_timeout mutex_test.km
   [ $status -eq 0 ]
}

@test "mem_test: threads create, malloc/free, exit and join (mem_test)" {
   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   run km_with_timeout mem_test.km
   [ "$status" -eq $expected_status ]
}

@test "pmem_test: test physical memory override (pmem_test)" {
   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 hello_test.km
   [ "$status" -ne 0 ]
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 hello_test.km
      [ "$status" -eq 0 ]
   fi
   # Don't support guest bus larger the host bus.
   run km_with_timeout -P `expr $(bus_width) + 1` hello_test.km
   [ "$status" -ne 0 ]
   # Don't support 0 width bus
   run km_with_timeout -P 0 hello_test.km
   [ "$status" -ne 0 ]
   run km_with_timeout -P -1 hello_test.km
   [ "$status" -ne 0 ]
}

@test "brk_map_test: test brk and map w/physical memory override (brk_map_test)" {
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 brk_map_test.km -- 33
      [ "$status" -eq 0 ]
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages brk_map_test.km -- 33
   [ "$status" -eq 1 ]
}

@test "cli: test 'km -v' and other small tests" {
   run km_with_timeout -v
   [ "$status" -eq 0 ]
   echo -e "$output" | grep -F -q `git rev-parse --abbrev-ref HEAD`
   echo -e "$output" | grep -F -q 'Kontain Monitor v'
   run km_with_timeout --version
   [ "$status" -eq 0 ]
}

@test "cpuid: test cpu vendor id (cpuid_test)" {
   run km_with_timeout cpuid_test.km
   [ "$status" -eq 0 ]
   echo -e "$output" | grep -F -q 'Kontain'
}

@test "longjmp_test: basic setjmp/longjump" {
   args="more_flags to_check: -f and check --args !"
   run ./longjmp_test $args
   [ $status -eq 0 ]
   linux_out="${output}"

   run km_with_timeout longjmp_test.km $args
   [ $status -eq 0 ]
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

# The behavior tested here is temporary and will change when real signal handling exists.
@test "exception: exceptions and faults in the guest (stray_test)" {
   CORE=/tmp/kmcore.$$
   # divide by zero
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km div0
   [ $status -eq 8 ] # SIGFPE
   echo $output | grep -F 'Floating point exception (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # invalid opcode
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km ud
   [ $status -eq 4 ] # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # page fault
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km stray
   [ $status -eq 11 ] # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # bad hcall
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km hc
   [ $status -eq 31 ] # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # write to text (protected memory)
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km prot
   [ $status -eq 11 ]  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # abort
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km abort
   [ $status -eq 6 ]  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # quit
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km quit
   [ $status -eq 3 ]  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   [ -f ${CORE} ]
   rm -f ${CORE}

   # term
   [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km term
   [ $status -eq 15 ]  # SIGTERM
   echo $output | grep -F 'Terminated'
   [ ! -f ${CORE} ]
}

@test "signals: signals in the guest (signals)" {
   run km_with_timeout signal_test.km
   [ $status -eq 0 ]
}


# C++ tests
@test "cpp: constructors and statics (var_storage_test)" {
   run km_with_timeout var_storage_test.km
   [ "$status" -eq 0 ]

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   [ "$ctors" -gt 0 ]
   [ "$ctors" -eq "$dtors" ]
}


@test "cpp: basic throw and unwind (throw_basic_test)" {
   run ./throw_basic_test
   [ $status -eq 0 ]
   linux_out="${output}"

   run km_with_timeout throw_basic_test.km
   [ "$status" -eq 0 ]

   diff <(echo -e "$linux_out")  <(echo -e "$output")
}
