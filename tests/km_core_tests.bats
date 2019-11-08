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
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_core_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "setup_link: check if linking produced text segment where we expect" {
   run objdump -wp load_test.km
   [[ $(echo -e "$output" | awk '/LOAD/{print $5}' | sort -g | head -1) -eq 0x200000 ]]

   run objdump -wp load_test.so
   [[ $(echo -e "$output" | awk '/LOAD/{print $5}' | sort -g | head -1) -eq 0x000000 ]]
}

@test "setup_basic: basic vm setup, workload invocation and exit value check (exit_value_test)" {
   for i in $(seq 1 200) ; do # a loop to catch race with return value, if any
      run km_with_timeout exit_value_test.km
      assert_equal $status 17

      run km_with_timeout ${KM_LDSO} exit_value_test.so
      assert_equal $status 17
   done
}

@test "setup_load: load elf and layout check (load_test)" {
   run km_with_timeout load_test.km
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   assert_success

   run km_with_timeout ${KM_LDSO} load_test.so -s
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   assert_success
}

@test "hc_check: invoke wrong hypercall (hc_test)" {
   run km_with_timeout stray_test.km hc 400
   assert [ $status == 31 ]  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.km hc -10
   assert [ $status == 31 ]  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.km hc 1000
   assert [ $status == 31 ]  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.km hc-badarg 3
   assert [ $status == 31 ]  #SIGSYS
   assert_output --partial "Bad system call"

   # shared TODO: core dump needs fixing.
#   run km_with_timeout ${KM_LDSO} stray_test.so hc 400
#   assert [ $status == 31 ]  #SIGSYS
#   assert_output --partial "Bad system call"
#
#   run km_with_timeout ${KM_LDSO} stray_test.so hc -10
#   assert [ $status == 31 ]  #SIGSYS
#   assert_output --partial "Bad system call"
#
#   run km_with_timeout ${KM_LDSO} stray_test.so hc 1000
#   assert [ $status == 31 ]  #SIGSYS
#   assert_output --partial "Bad system call"
#
#   run km_with_timeout ${KM_LDSO} stray_test.so hc -badarg 3
#   assert [ $status == 31 ]  #SIGSYS
#   assert_output --partial "Bad system call"
}

@test "km_main: wait on signal (hello_test)" {
   run timeout -s SIGUSR1 1s ${KM_BIN} --wait-for-signal hello_test.km
   [ $status -eq 124 ]

   run timeout -s SIGUSR1 1s ${KM_BIN} --wait-for-signal ${KM_LDSO} hello_test.so
   [ $status -eq 124 ]
}

@test "km_main: optargs (hello_test)" {
   # -v flag prints version and branch
   run km_with_timeout -v hello_test.km
   assert_success
   branch=$BRANCH
   assert_line --partial "$branch"

   run km_with_timeout -Vkvm hello_test.km # -V<regex> turns on tracing for a subsystem. Check it for -Vkvm
   assert_success
   assert_line --partial "KVM_EXIT_IO"

   # -g[port] turns on gdb and tested in gdb coverage. Let's validate a failure case
   run km_with_timeout -gfoobar hello_test.km
   assert_failure
   assert_line  "km: Wrong gdb port number 'foobar'"

   corefile=/tmp/km$$
   run km_with_timeout -Vcoredump -C $corefile hello_test.km # -C sets coredump file name
   assert_success
   assert_output --partial "Setting coredump path to $corefile"

   run km_with_timeout -P 31 hello_test.km # -P sets Physical memory bus width
   assert_failure
   assert_line  "km: Guest memory bus width must be between 32 and 63 - got '31'"

   run km_with_timeout -P32 hello_test.km
   assert_success

   run km_with_timeout -X # invalid option
   assert_failure
   assert_line --partial "invalid option"

   # KM will auto-add '.km' to file name, so create a .km file with Linux executable
   tmp=/tmp/hello$$ ; cp hello_test $tmp.km
   run km_with_timeout $tmp # Linux executable instead of .km
   assert_failure
   assert_line "km: Non-KM binary: cannot find interrupt handler(*) or sigreturn(*). Trying to run regular Linux executable in KM?"
   rm $tmp.km # may leave dirt if the tests above fail

   log=`mktemp`
   echo Log location: $log
   run km_with_timeout -V --log-to=$log hello_test.km # check --log-to option
   assert_success
   assert [ -e $log ]
   assert grep -q 'Hello, world' $log       # stdout
   assert grep -q 'Setting VendorId ' $log  # stderr
   rm $log
   run km_with_timeout -V --log-to=/very/bad/place hello_test.km
   assert_failure

   # Shared
   # -v flag prints version and branch
   run km_with_timeout -v ${KM_LDSO} hello_test.so
   assert_success
   branch=$BRANCH
   assert_line --partial "$branch"
   
   run km_with_timeout -Vkvm ${KM_LDSO} hello_test.so # -V<regex> turns on tracing for a subsystem. Check it for -Vkvm
   assert_success
   assert_line --partial "KVM_EXIT_IO"

   # -g[port] turns on gdb and tested in gdb coverage. Let's validate a failure case
   run km_with_timeout -gfoobar ${KM_LDSO} hello_test.so
   assert_failure
   assert_line  "km: Wrong gdb port number 'foobar'"

   corefile=/tmp/km$$
   run km_with_timeout -Vcoredump -C $corefile ${KM_LDSO} hello_test.so # -C sets coredump file name
   assert_success
   assert_output --partial "Setting coredump path to $corefile"

   run km_with_timeout -P 31 ${KM_LDSO} hello_test.so # -P sets Physical memory bus width
   assert_failure
   assert_line  "km: Guest memory bus width must be between 32 and 63 - got '31'"

   run km_with_timeout -P32 ${KM_LDSO} hello_test.so
   assert_success

   # KM will auto-add '.km' to file name, so create a .km file with Linux executable
   tmp=/tmp/hello$$ ; cp hello_test $tmp.km
   run km_with_timeout $tmp # Linux executable instead of .km
   assert_failure
   assert_line "km: Non-KM binary: cannot find interrupt handler(*) or sigreturn(*). Trying to run regular Linux executable in KM?"
   rm $tmp.km # may leave dirt if the tests above fail

   log=`mktemp`
   echo Log location: $log
   run km_with_timeout -V --log-to=$log ${KM_LDSO} hello_test.so # check --log-to option
   assert_success
   assert [ -e $log ]
   assert grep -q 'Hello, world' $log       # stdout
   assert grep -q 'Setting VendorId ' $log  # stderr
   rm $log
   run km_with_timeout -V --log-to=/very/bad/place ${KM_LDSO} hello_test.so
   assert_failure
}

@test "km_main: passing environment to payloads (env_test)" {
   val=`pwd`/$$
   run km_with_timeout --putenv PATH=$val env_test.km
   assert_success
   assert_output --partial "PATH=$val"

   run km_with_timeout --copyenv --copyenv env_test.km
   assert_success
   assert_line "km: Ignoring redundant '--copyenv' option"
   assert_line "getenv: PATH=$PATH"


   run km_with_timeout --copyenv --putenv MORE=less env_test.km
   assert_failure

   run km_with_timeout --putenv PATH=testingpath --copyenv env_test.km
   assert_failure

   # shared
   run km_with_timeout --putenv PATH=$val ${KM_LDSO} env_test.so
   assert_success
   assert_output --partial "PATH=$val"

   run km_with_timeout --copyenv --copyenv ${KM_LDSO} env_test.so
   assert_success
   assert_line "km: Ignoring redundant '--copyenv' option"
   assert_line "getenv: PATH=$PATH"

   run km_with_timeout --copyenv --putenv MORE=less ${KM__LDSO} env_test.so
   assert_failure

   run km_with_timeout --putenv PATH=testingpath --copyenv ${KM_LDSO} env_test.so
   assert_failure
}

@test "mem_slots: KVM memslot / phys mem sizes (memslot_test)" {
   run ./memslot_test
   assert_success
}

@test "mem_regions: Crossing regions boundary (regions_test)" {
   run ${KM_BIN} regions_test.km
   assert_success

   run ${KM_BIN} ${KM_LDSO} regions_test.so
   assert_success
}

@test "mem_brk: brk() call (brk_test)" {
   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   run km_with_timeout --overcommit-memory brk_test.km
   assert [ $status -eq $expected_status ]

   run km_with_timeout --overcommit-memory ${KM_LDSO} brk_test.so
   assert [ $status -eq $expected_status ]
}

@test "hc_basic: basic run and print hello world (hello_test)" {
   args="more_flags to_check: -f and check --args !"
   run ./hello_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout hello_test.km $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')

   # shared
   run km_with_timeout ${KM_LDSO} hello_test.so $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "hc_socket: basic HTTP/socket I/O (hello_html_test)" {
   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   sleep 0.5s
   run curl -s $address
   assert_success
   linux_out="${output}"

   (km_with_timeout hello_html_test.km &)
   sleep 0.5s
	run curl -s $address
   assert_success
   diff <(echo -e "$linux_out")  <(echo -e "$output")

   # shared 
   (km_with_timeout ${KM_LDSO} hello_html_test.so &)
   sleep 0.5s
	 run curl -s $address
   assert_success
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mem_mmap: mmap and munmap with addr=0 (mmap_test)" {
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run km_with_timeout mmap_test.km -v
   [ $status -eq $expected_status ]

   # shared - TODO - 1 failure out of 6 tests.
   #run km_with_timeout ${KM_LDSO} mmap_test.so -v
   #[ $status -eq $expected_status ]
}

@test "futex example" {
   skip "TODO: convert to test"

   run km_with_timeout futex.km
   assert_success

   run km_with_timeout ${KM_LDSO} futex.so
   assert_success
}

@test "gdb_basic: gdb support (gdb_test)" {
   km_gdb_default_port=2159
   # start KM in background, give it time to start, and connect with gdb cliennt
   km_with_timeout -g gdb_test.km &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test.km
   # check that gdb found what it is supposed to find
   assert_line --partial 'SUCCESS'
   # check that KM exited normally
   wait $gdb_pid
   assert_success

   # TODO: shared objects
}

# Test with signals
@test "gdb_signal: gdb signal support (stray_test)" {
   km_gdb_default_port=2159
   km_with_timeout -g stray_test.km signal &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_signal_test.gdb" \
         --ex=c --ex=q stray_test.km
   assert_success
   assert_line --partial 'received signal SIGUSR1'
   assert_line --partial 'received signal SIGABRT'
   # check that KM exited normally
   run wait $gdb_pid
   assert [ $status -eq 6 ]

   # TODO: shared objects
}

@test "gdb_exception: gdb exception support (stray_test)" {
   km_gdb_default_port=2159
   # Test with signals
   km_with_timeout -g stray_test.km stray &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_exception_test.gdb" \
         --ex=c --ex=q stray_test.km
   assert_success
   assert_line --partial  'received signal SIGSEGV'
   # check that KM exited normally
   run wait $gdb_pid
   assert [ $status -eq 11 ]  # SIGSEGV

   # TODO: shared objects
}

@test "Unused memory protection: check that unused memory is protected (mprotect_test)" {
   run km_with_timeout mprotect_test.km -v
   assert_success

   run km_with_timeout ${KM_LDSO} mprotect_test.so -v
   assert_success
}

@test "threads_basic: threads with TLS, create, exit and join (hello_2_loops_tls_test)" {
   run km_with_timeout hello_2_loops_tls_test.km
   assert_success
   refute_line --partial 'BAD'

   # TODO: error loading ld-linux-x86-64.so !!! fix this (shared)
   #run km_with_timeout ${KM_LDSO} hello_2_loops_tls_test.so
   #assert_success
   #refute_line --partial 'BAD'
}

@test "threads_basic: threads with TSD, create, exit and join (hello_2_loops_test)" {
   run km_with_timeout hello_2_loops_test.km
   assert_success

   # shared
   run km_with_timeout ${KM_LDSO} hello_2_loops_test.so
   assert_success
}

@test "threads_exit_grp: force exit when threads are in flight (exit_grp_test)" {
   run km_with_timeout exit_grp_test.km
   # the test can exit(17) from main thread or random exit(11) from subthread
   assert [ $status -eq 17 -o $status -eq 11  ]

   # shared
   run km_with_timeout ${KM_LDSO} exit_grp_test.so
   # the test can exit(17) from main thread or random exit(11) from subthread
   assert [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex: mutex (mutex_test)" {
   run km_with_timeout mutex_test.km
   assert_success

   # shared
   run km_with_timeout ${KM_LDSO} mutex_test.so
   assert_success
}

@test "mem_test: threads create, malloc/free, exit and join (mem_test)" {
   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   run km_with_timeout mem_test.km
   assert [ $status -eq $expected_status ]

   # shared
   run km_with_timeout ${KM_LDSO} mem_test.so
   assert [ $status -eq $expected_status ]
}

@test "pmem_test: test physical memory override (pmem_test)" {
   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 hello_test.km
   assert_failure
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 hello_test.km
      assert_success
   fi
   run km_with_timeout -P `expr $(bus_width) + 1` hello_test.km # Don't support guest bus larger the host bus
   assert_failure
   run km_with_timeout -P 0 hello_test.km # Don't support 0 width bus
   assert_failure
   run km_with_timeout -P -1 hello_test.km
   assert_failure

   # Shared
   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 ${KM_LDSO} hello_test.so
   assert_failure
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 ${KM_LDSO} hello_test.so
      assert_success
   fi
   run km_with_timeout -P `expr $(bus_width) + 1` ${KM_LDSO} hello_test.so # Don't support guest bus larger the host bus
   assert_failure
   run km_with_timeout -P 0 ${KM_LDDO} hello_test.so # Don't support 0 width bus
   assert_failure
   run km_with_timeout -P -1 ${KM_LDSO} hello_test.so
   assert_failure
}

@test "brk_map_test: test brk and map w/physical memory override (brk_map_test)" {
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 brk_map_test.km -- 33
      assert_success
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages brk_map_test.km -- 33
   assert_failure

   # Shared
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 ${KM_LDSO} brk_map_test.so -- 33
      assert_success
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages ${KM_LDSO} brk_map_test.so -- 33
   assert_failure
}

@test "cli: test 'km -v' and other small tests" {
   run km_with_timeout -v
   assert_success
   assert_line --partial `git rev-parse --abbrev-ref HEAD`
   assert_line --partial 'Kontain Monitor v'
   run km_with_timeout --version
   assert_success
}

@test "cpuid: test cpu vendor id (cpuid_test)" {
   run km_with_timeout cpuid_test.km
   assert_success
   assert_line --partial 'Kontain'

   run km_with_timeout ${KM_LDSO} cpuid_test.so
   assert_success
   assert_line --partial 'Kontain'
}

@test "longjmp_test: basic setjmp/longjump" {
   args="more_flags to_check: -f and check --args !"
   run ./longjmp_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout longjmp_test.km $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')

   run km_with_timeout ${KM_LDSO} longjmp_test.so $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "exception: exceptions and faults in the guest (stray_test)" {
   CORE=/tmp/kmcore.$$
   # divide by zero
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km div0
   assert [ $status -eq 8 ] # SIGFPE
   echo $output | grep -F 'Floating point exception (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'div0 ('
   # Check number of segments. Shoudl be 8
   nload=`readelf -l ${CORE} | grep LOAD | wc -l`
   assert [ "${nload}" == "8" ]
   rm -f ${CORE}

   # invalid opcode
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km ud
   assert [ $status -eq 4 ] # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'undefined_op ('
   rm -f ${CORE}

   # page fault
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km stray
   assert [ $status -eq 11 ] # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # bad hcall
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km hc 400
   assert [ $status -eq 31 ] # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'main ('
   rm -f ${CORE}

   # write to text (protected memory)
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km prot
   assert [ $status -eq 11 ]  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'write_text ('
   assert rm -f ${CORE}

   # abort
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km abort
   assert [ $status -eq 6 ]  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'abort ('
   rm -f ${CORE}

   # quit
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km quit
   assert [ $status -eq 3 ]  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'kill ('
   rm -f ${CORE}

   # term
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km term
   assert [ $status -eq 15 ]  # SIGTERM
   echo $output | grep -F 'Terminated'
   assert [ ! -f ${CORE} ]

   # signal
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km signal
   assert [ $status -eq 6 ]  # SIGABRT
   echo $output | grep -F 'Aborted'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'abort ('
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'signal_abort_handler ('
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F '<signal handler called>'
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'signal_abort_test ('
   rm -f ${CORE}

   # sigsegv blocked
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km block-segv
   assert [ $status -eq 11 ]  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.km ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # ensure that the guest can ignore a SIGPIPE.
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.km sigpipe
   assert_success  # should succeed
   assert [ ! -f ${CORE} ]

   # Try to close a KM file from the guest
   run km_with_timeout --coredump=${CORE} stray_test.km close 5
   assert [ $status -eq 9 ]  # EBADF
   assert [ ! -f ${CORE} ]

   # TODO: Core file for .so
}

@test "signals: signals in the guest (signals)" {
   run km_with_timeout signal_test.km -v
   assert_success

   run km_with_timeout ${KM_LDSO} signal_test.so -v
   assert_success
}

@test "pthread_cancel: (pthread_cancel_test)" {
   run km_with_timeout pthread_cancel_test.km -v
   assert_success

   run km_with_timeout ${KM_LDSO} pthread_cancel_test.so -v
   assert_success
}

# C++ tests
@test "cpp: constructors and statics (var_storage_test)" {
   run km_with_timeout var_storage_test.km
   assert_success

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]

   # TODO: CPP for .so
}


@test "cpp: basic throw and unwind (throw_basic_test)" {
   run ./throw_basic_test
   assert_success
   linux_out="${output}"

   run km_with_timeout throw_basic_test.km
   [ "$status" -eq 0 ]

   diff <(echo -e "$linux_out")  <(echo -e "$output")

   # TODO: CPP for .so
   #run km_with_timeout ${KM_LDSO} throw_basic_test.so
   #[ "$status" -eq 0 ]
}

@test "filesys: guest file system operations (filesys_test)" {
   run km_with_timeout filesys_test.km -v
   assert_success
   run km_with_timeout ${KM_LDSO} filesys_test.so -v
   assert_success
}

@test "filepath: guest file path operations (filepathtest)" {
   DIRNAME=`mktemp -d`
   # note: the 2nd parameter (500) is the number o time the
   #       concurrent_open_test runs in a loop. The3 default is
   #       10000. We use 500 here to accomodate azure, where the
   #       open/close cycle is ~40ms vs 1ms on a local workstation.
   run km_with_timeout filepath_test.km ${DIRNAME} 500
   assert_success
   rm -rf /tmp/${DIRNAME}

   # TODO: core dumped
   #DIRNAME=`mktemp -d`
   #run km_with_timeout ${KM_LDSO} filepath_test.so ${DIRNAME} 500
   #assert_success
   #rm -rf /tmp/${DIRNAME}
}

@test "socket: guest socket operations (socket_test)" {
   run km_with_timeout socket_test.km
   assert_success

   run km_with_timeout ${KM_LDSO} socket_test.so
   assert_success
}

@test "dl_iterate_phdr: AUXV and dl_iterate_phdr (dl_iterate_phdr_test)" {
   run km_with_timeout dl_iterate_phdr_test.km -v
   assert_success

   # TODO: core dump
   #run km_with_timeout ${KM_LDSO} dl_iterate_phdr_test.so -v
   #assert_success
}

# dlopen
@test "dlopen_test.so: simple dlopen (dlopen_test)" {
   run km_with_timeout ${KM_LDSO} dlopen_test.so
   assert_success

}