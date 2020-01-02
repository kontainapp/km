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

# Lists of tests to skip (space separated)
# not_needed_{generic,static,dynamic,shared} - skip since it's not needed
# todo_{generic,static,dynamic,shared} - skip since it's a TODO
not_needed_generic=""
not_needed_static=""
not_needed_dynamic="setup_load mem_slots cli km_main_env" # note:env is tested with KM_COMMAND
not_needed_so="setup_load cli"
todo_generic="futex_example"
todo_static=""
todo_dynamic="mem_mmap exception cpp_ctors dl_iterate_phdr monitor_maps "
todo_so="hc_check mem_slots mem_mmap gdb_basic gdb_signal gdb_exception gdb_server_race gdb_qsupported exception cpp_ctors dl_iterate_phdr monitor_maps mem_mmap_1"


# Now the actual tests.
# They can be invoked by either 'make test [MATCH=<filter>]' or ./km_core_tests.bats [-f <filter>]
# <filter> is a regexp or substring matching test name

@test "setup_link($test_type): check if linking produced text segment where we expect" {
   run objdump -wp load_test$ext
   if [ $test_type == so ] ; then
      expected_location=0x000000
   else
      expected_location=0x200000
   fi
   [[ $(echo -e "$output" | awk '/LOAD/{print $5}' | sort -g | head -1) -eq $expected_location ]]
}

@test "setup_basic($test_type): basic vm setup, workload invocation and exit value check (exit_value_test$ext)" {
   for i in $(seq 1 200) ; do # a loop to catch race with return value, if any
      run km_with_timeout exit_value_test$ext
      assert_failure 17
   done
}

@test "setup_load($test_type): load elf and layout check (load_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout load_test$ext
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   assert_success
}

@test "hc_check($test_type): invoke wrong hypercall (hc_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout stray_test$ext hc 400
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test$ext hc -10
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test$ext hc 1000
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test$ext hc-badarg 3
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

}

@test "km_main_signal($test_type): wait on signal (hello_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi
   run timeout -s SIGUSR1 1s ${KM_BIN} --dynlinker=${KM_LDSO} --wait-for-signal hello_test$ext
   assert_failure 124
}

@test "km_main_args($test_type): optargs (hello_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   # -v flag prints version and branch
   run km_with_timeout -v -- hello_test$ext
   assert_success
   branch=$BRANCH
   assert_line --partial "$branch"

   run km_with_timeout -Vkvm -- hello_test$ext # -V<regex> turns on tracing for a subsystem. Check it for -Vkvm
   assert_success
   assert_line --partial "KVM_EXIT_IO"

   # -g[port] turns on gdb and tested in gdb coverage. Let's validate a failure case
   run km_with_timeout -gfoobar -- hello_test$ext
   assert_failure
   assert_line  "km: Wrong gdb port number 'foobar'"

   corefile=/tmp/km$$
   run km_with_timeout -Vcoredump -C $corefile -- hello_test$ext # -C sets coredump file name
   assert_success
   assert_output --partial "Setting coredump path to $corefile"

   run km_with_timeout -P 31 -- hello_test$ext # -P sets Physical memory bus width
   assert_failure
   assert_line  "km: Guest memory bus width must be between 32 and 63 - got '31'"

   run km_with_timeout -P32 -- hello_test$ext
   assert_success

   run km_with_timeout -X # invalid option
   assert_failure
   assert_line --partial "invalid option"

   # KM will auto-add '.km' to file name, so create a .km file with Linux executable
   tmp=/tmp/hello$$ ; cp hello_test $tmp.km
   run km_with_timeout $tmp # Linux executable instead of $ext
   assert_failure
   assert_line "km: PT_INTERP does not contain km marker. expect:'__km_dynlink__' got:'/lib64/ld-linux-x86-64.so.2'"   XXXXXXX
   rm $tmp.km # may leave dirt if the tests above fail

   log=`mktemp`
   echo Log location: $log
   run km_with_timeout -V --log-to=$log -- hello_test$ext # check --log-to option
   assert_success
   assert [ -e $log ]
   assert grep -q 'Hello, world' $log       # stdout
   assert grep -q 'Setting VendorId ' $log  # stderr
   rm $log
   run km_with_timeout -V --log-to=/very/bad/place -- hello_test$ext
   assert_failure
}

@test "km_main_env($test_type): passing environment to payloads (env_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   val=`pwd`/$$
   run km_with_timeout --putenv PATH=$val env_test$ext
   assert_success
   assert_output --partial "PATH=$val"

   run km_with_timeout --copyenv --copyenv env_test$ext
   assert_success
   assert_line "km: Ignoring redundant '--copyenv' option"
   assert_line "getenv: PATH=$PATH"


   run km_with_timeout --copyenv --putenv MORE=less env_test$ext
   assert_failure

   run km_with_timeout --putenv PATH=testingpath --copyenv env_test$ext
   assert_failure
}

@test "mem_slots($test_type): KVM memslot / phys mem sizes (memslot_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run ./memslot_test
   assert_success
}

@test "mem_regions($test_type): Crossing regions boundary (regions_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run ${KM_BIN} ${KM_ARGS} regions_test$ext
   assert_success
}

@test "mem_brk($test_type): brk() call (brk_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   run km_with_timeout --overcommit-memory brk_test$ext
   assert [ $status -eq $expected_status ]
}

@test "hc_basic($test_type): basic run and print hello world (hello_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   args="more_flags to_check: -f and check --args !"
   run ./hello_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout hello_test$ext $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "hc_socket($test_type): basic HTTP/socket I/O (hello_html_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   sleep 0.5s
   run curl -s $address
   assert_success
   linux_out="${output}"

   (km_with_timeout hello_html_test$ext &)
   sleep 0.5s
	run curl -s $address
   assert_success
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mem_mmap($test_type): mmap and munmap with addr=0 (mmap_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run km_with_timeout mmap_test$ext -v
   assert [ $status -eq $expected_status ]
}

@test "mem_mmap_1($test_type): mmap then smaller mprotect (mmap_1_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout mmap_1_test$ext
   assert_success
}

@test "futex_example($test_type)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout futex$ext
   assert_success
}

@test "gdb_basic($test_type): gdb support (gdb_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   km_gdb_default_port=2159
   # start KM in background, give it time to start, and connect with gdb client
   km_with_timeout -g gdb_test$ext &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test$ext
   # check that gdb found what it is supposed to find
   assert_line --partial 'SUCCESS'
   # check that KM exited normally
   wait $gdb_pid
   assert_success
}

# Test with signals
@test "gdb_signal($test_type): gdb signal support (stray_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   km_gdb_default_port=2159
   km_with_timeout -g stray_test$ext signal &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_signal_test.gdb" \
         --ex=c --ex=q stray_test$ext
   assert_success
   assert_line --partial 'received signal SIGUSR1'
   assert_line --partial 'received signal SIGABRT'
   # check that KM exited normally
   run wait $gdb_pid
   assert_failure 6
}

@test "gdb_exception($test_type): gdb exception support (stray_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   km_gdb_default_port=2159
   # Test with signals
   km_with_timeout -g stray_test$ext stray &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_exception_test.gdb" \
         --ex=c --ex=q stray_test$ext
   assert_success
   assert_line --partial  'received signal SIGSEGV'
   # check that KM exited normally
   run wait $gdb_pid
   assert_failure 11  # SIGSEGV
}

@test "gdb_server_race($test_type): gdb server concurrent wakeup test" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   km_gdb_default_port=2159
   km_trace_file=/tmp/gdb_server_race_test_static_$$.out
   # Test with breakpoints triggering and SIGILL being happending continuously
   # Save output to a log file for our own check using grep below.
   km_with_timeout -V -g gdb_server_entry_race_test$ext >$km_trace_file 2>&1 &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_gdbserverrace_test.gdb" \
         --ex=c --ex=q gdb_server_entry_race_test$ext
   assert_success

   # check that KM exited normally
   run wait $gdb_pid
   assert_success

   # look for km trace entries that show the sigill signal overrode the breakpoint
   # when deciding to tell the gdb client why we stopped.
   grep "overriding pending signal" $km_trace_file >/dev/null
   assert_success
   # rm -f $km_trace_file
}

@test "gdb_qsupported($test_type): gdb qsupport/vcont test" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   km_gdb_default_port=2159
   # Verify that qSupported, vCont?, vCont, and qXfer:threads:read remote
   # commands are being used.
   km_with_timeout -g gdb_qsupported_test$ext &
   gdb_pid=$!; sleep 0.5
   run gdb_with_timeout -q -nx --ex="set debug remote 1" --ex="target remote :$km_gdb_default_port" \
      --ex="source cmd_for_qsupported_test.gdb" --ex=q gdb_qsupported_test$ext
   assert_success

   # Verify that km gdb server is responding to a qSupported packet
   assert_line --partial "Packet received: PacketSize=00003FFF;qXfer:threads:read+;swbreak+;hwbreak+;vContSupported+"
   # Verify that km gdb server is responding to a vCont? packet
   assert_line --partial "Packet received: vCont;c;s;C;S"
   # Verify that km gdb server is responding to a qXfer:threads:read" packet
   assert_line --partial "Packet received: m<threads>\\n  <thread id"
   # Verify that the gdb client is using vCont packets
   assert_line --partial "Sending packet: \$vCont;s:"
   if [[ "$check_hwbreak" == yes ]] ; then
      # Verify gdb server stop reply packet for hardware break
      assert_line --partial "Packet received: T05hwbreak:;thread:"
   fi
   # Verify that we now see the switching threads message
   assert_line --partial "Switching to Thread 2"

   run wait $gdb_pid
   assert_success
}

@test "unused_memory_protection($test_type): check that unused memory is protected (mprotect_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout mprotect_test$ext -v
   assert_success
}

@test "threads_basic_tls($test_type): threads with TLS, create, exit and join (hello_2_loops_tls_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout hello_2_loops_tls_test$ext
   assert_success
   if [ $test_type != "static" ] ; then
      refute_line --partial 'BAD'
   fi
}

@test "threads_basic_tsd($test_type): threads with TSD, create, exit and join (hello_2_loops_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout hello_2_loops_test$ext
   assert_success
}

@test "threads_exit_grp($test_type): force exit when threads are in flight (exit_grp_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout exit_grp_test$ext
   # the test can exit(17) from main thread or random exit(11) from subthread
   assert [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex($test_type): mutex (mutex_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout mutex_test$ext
   assert_success
}

@test "mem_test($test_type): threads create, malloc/free, exit and join (mem_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   run km_with_timeout mem_test$ext
   assert [ $status -eq $expected_status ]
}

@test "pmem_test($test_type): test physical memory override (hello_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 hello_test$ext
   assert_failure
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 hello_test$ext
      assert_success
   fi
   run km_with_timeout -P `expr $(bus_width) + 1` hello_test$ext # Don't support guest bus larger the host bus
   assert_failure
   run km_with_timeout -P 0 hello_test$ext # Don't support 0 width bus
   assert_failure
   run km_with_timeout -P -1 hello_test$ext
   assert_failure
}

@test "brk_map_test($test_type): test brk and map w/physical memory override (brk_map_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 brk_map_test$ext -- 33
      assert_success
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages brk_map_test$ext -- 33
   assert_failure
}

@test "cli($test_type): test 'km -v' and other small tests" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout -v
   assert_success
   assert_line --partial `git rev-parse --abbrev-ref HEAD`
   assert_line --partial 'Kontain Monitor v'
   run km_with_timeout --version
   assert_success
}

@test "cpuid($test_type): test cpu vendor id (cpuid_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout cpuid_test$ext
   assert_success
   assert_line --partial 'Kontain'
}

@test "longjmp_test($test_type): basic setjmp/longjump" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   args="more_flags to_check: -f and check --args !"
   run ./longjmp_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout longjmp_test$ext $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "exception($test_type): exceptions and faults in the guest (stray_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi
   # NOTE: gdb tries to use PT_INTERP. Doesn't like __km_dynlink__ in dynamic test

   CORE=/tmp/kmcore.$$
   # divide by zero
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext div0
   assert_failure 8 # SIGFPE
   echo $output | grep -F 'Floating point exception (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'div0 ('
   # Check number of segments. Shoudl be 8
   nload=`readelf -l ${CORE} | grep LOAD | wc -l`
   assert [ "${nload}" == "9" ]
   rm -f ${CORE}

   # invalid opcode
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext ud
   assert_failure 4 # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'undefined_op ('
   rm -f ${CORE}

   # page fault
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext stray
   assert_failure 11 # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # bad hcall
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext hc 400
   assert_failure 31 # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'main ('
   rm -f ${CORE}

   # write to text (protected memory)
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext prot
   assert_failure 11  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'write_text ('
   assert rm -f ${CORE}

   # abort
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext abort
   assert_failure 6  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'abort ('
   rm -f ${CORE}

   # quit
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext quit
   assert_failure 3  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'kill ('
   rm -f ${CORE}

   # term
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext term
   assert_failure 15  # SIGTERM
   echo $output | grep -F 'Terminated'
   assert [ ! -f ${CORE} ]

   # signal
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext signal
   assert_failure 6  # SIGABRT
   echo $output | grep -F 'Aborted'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'abort ('
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'signal_abort_handler ('
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F '<signal handler called>'
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'signal_abort_test ('
   rm -f ${CORE}

   # sigsegv blocked
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext block-segv
   assert_failure 11  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # ensure that the guest can ignore a SIGPIPE.
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext sigpipe
   assert_success  # should succeed
   assert [ ! -f ${CORE} ]

   # Try to close a KM file from the guest
   run km_with_timeout --coredump=${CORE} stray_test$ext close 5
   assert_failure 9  # EBADF
   assert [ ! -f ${CORE} ]
}

@test "signals($test_type): signals in the guest (signals)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout signal_test$ext -v
   assert_success
}

@test "pthread_cancel($test_type): (pthread_cancel_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout pthread_cancel_test$ext -v
   assert_success
}

# C++ tests
@test "cpp_ctors($test_type): constructors and statics (var_storage_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout var_storage_test$ext
   assert_success

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]
}


@test "cpp_throw($test_type): basic throw and unwind (throw_basic_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run ./throw_basic_test
   assert_success
   linux_out="${output}"

   run km_with_timeout throw_basic_test$ext
   assert_success

   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "filesys($test_type): guest file system operations (filesys_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout filesys_test$ext -v
   assert_success
}

@test "filepath($test_type): guest file path operations (filepathtest$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   DIRNAME=`mktemp -d`
   # note: the 2nd parameter (500) is the number o time the
   #       concurrent_open_test runs in a loop. The3 default is
   #       10000. We use 500 here to accomodate azure, where the
   #       open/close cycle is ~40ms vs 1ms on a local workstation.
   run km_with_timeout filepath_test$ext ${DIRNAME} 500
   assert_success
   rm -rf /tmp/${DIRNAME}
}

@test "socket($test_type): guest socket operations (socket_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout socket_test$ext
   assert_success
}

@test "dl_iterate_phdr($test_type): AUXV and dl_iterate_phdr (dl_iterate_phdr_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout dl_iterate_phdr_test$ext -v
   assert_success
}

@test "monitor_maps($test_type): munmap gdt and idt (munmap_monitor_maps_test$ext)" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout munmap_monitor_maps_test$ext
   assert_success
}

@test "hypercall args($test_type): test hcall args passing" {
   reason=$(skip_is_needed "$BATS_TEST_DESCRIPTION")
   if [ -n "$reason" ] ; then skip "$reason"; fi

   run km_with_timeout --overcommit-memory hcallargs_test$ext
   assert_success
}
