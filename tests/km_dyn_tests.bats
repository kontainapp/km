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

@test "setup_link(dynamic): check if linking produced text segment where we expect" {
   run objdump -wp load_test.kmd
   [[ $(echo -e "$output" | awk '/LOAD/{print $5}' | sort -g | head -1) -eq 0x200000 ]]
}

@test "setup_basic(dynamic): basic vm setup, workload invocation and exit value check (exit_value_test)" {
   for i in $(seq 1 200) ; do # a loop to catch race with return value, if any
      run km_with_timeout exit_value_test.kmd
      assert_failure 17
   done
}

@test "hc_check(dynamic): invoke wrong hypercall (hc_test)" {
   run km_with_timeout stray_test.kmd hc 400
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.kmd hc -10
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.kmd hc 1000
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test.kmd hc-badarg 3
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

}

@test "km_main(dynamic): wait on signal (hello_test)" {
   run timeout -s SIGUSR1 1s ${KM_BIN} --dynlinker=${KM_LDSO} --wait-for-signal hello_test.kmd
   assert_failure 124
}

@test "km_main(dynamic): optargs (hello_test)" {
   # -v flag prints version and branch
   run km_with_timeout -v hello_test.kmd
   assert_success
   branch=$BRANCH
   assert_line --partial "$branch"

   run km_with_timeout -Vkvm hello_test.kmd # -V<regex> turns on tracing for a subsystem. Check it for -Vkvm
   assert_success
   assert_line --partial "KVM_EXIT_IO"

   # -g[port] turns on gdb and tested in gdb coverage. Let's validate a failure case
   run km_with_timeout -gfoobar hello_test.kmd
   assert_failure
   assert_line  "km: Wrong gdb port number 'foobar'"

   corefile=/tmp/km$$
   run km_with_timeout -Vcoredump -C $corefile hello_test.kmd # -C sets coredump file name
   assert_success
   assert_output --partial "Setting coredump path to $corefile"

   run km_with_timeout -P 31 hello_test.kmd # -P sets Physical memory bus width
   assert_failure
   assert_line  "km: Guest memory bus width must be between 32 and 63 - got '31'"

   run km_with_timeout -P32 hello_test.kmd
   assert_success

   run km_with_timeout -X # invalid option
   assert_failure
   assert_line --partial "invalid option"

   # KM will auto-add '.km' to file name, so create a .km file with Linux executable
   tmp=/tmp/hello$$ ; cp hello_test $tmp.km
   run km_with_timeout $tmp # Linux executable instead of .km
   assert_failure
   assert_line --partial "km: PT_INTERP does not contain km marker. expect:'__km_dynlink__'"
   rm $tmp.km # may leave dirt if the tests above fail

   log=`mktemp`
   echo Log location: $log
   run km_with_timeout -V --log-to=$log hello_test.kmd # check --log-to option
   assert_success
   assert [ -e $log ]
   assert grep -q 'Hello, world' $log       # stdout
   assert grep -q 'Setting VendorId ' $log  # stderr
   rm $log
   run km_with_timeout -V --log-to=/very/bad/place hello_test.kmd
   assert_failure

}

@test "km_main(dynamic): passing environment to payloads (env_test)" {
   val=`pwd`/$$
   run km_with_timeout --putenv PATH=$val env_test.kmd
   assert_success
   assert_output --partial "PATH=$val"

   run km_with_timeout --copyenv --copyenv env_test.kmd
   assert_success
   assert_line "km: Ignoring redundant '--copyenv' option"
   assert_line "getenv: PATH=$PATH"


   run km_with_timeout --copyenv --putenv MORE=less env_test.kmd
   assert_failure

   run km_with_timeout --putenv PATH=testingpath --copyenv env_test.kmd
   assert_failure
}

@test "mem_slots(dynamic): KVM memslot / phys mem sizes (memslot_test)" {
   skip "Run as part of static"
   run ./memslot_test
   assert_success
}

@test "mem_regions(dynamic): Crossing regions boundary (regions_test)" {
   run ${KM_BIN} --dynlinker=${KM_LDSO} regions_test.kmd
   assert_success
}

@test "mem_brk(dynamic): brk() call (brk_test)" {
   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   run km_with_timeout --overcommit-memory brk_test.kmd
   assert [ $status -eq $expected_status ]
}

@test "hc_basic(dynamic): basic run and print hello world (hello_test)" {
   args="more_flags to_check: -f and check --args !"
   run ./hello_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout hello_test.kmd $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "hc_socket(dynamic): basic HTTP/socket I/O (hello_html_test)" {
   local address="http://127.0.0.1:8002"

   (./hello_html_test &)
   sleep 0.5s
   run curl -s $address
   assert_success
   linux_out="${output}"

   (km_with_timeout hello_html_test.kmd &)
   sleep 0.5s
	run curl -s $address
   assert_success
   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "mem_mmap(dynamic): mmap and munmap with addr=0 (mmap_test)" {
   skip "TODO: need to fix"
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run km_with_timeout mmap_test.kmd -v
   assert [ $status -eq $expected_status ]
}

@test "mem_mmap_1(static): mmap then smaller mprotect (mmap_1_test)" {
   assert_success
   run km_with_timeout mmap_1_test.km
}

@test "futex example(dynamic)" {
   skip "TODO: convert to test"

   run km_with_timeout futex.kmd
   assert_success
}

@test "gdb_basic(dynamic): gdb support (gdb_test)" {
   km_gdb_default_port=2159
   # start KM in background, give it time to start, and connect with gdb cliennt
   km_with_timeout -g gdb_test.kmd &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test.kmd
   # check that gdb found what it is supposed to find
   assert_line --partial 'SUCCESS'
   # check that KM exited normally
   wait $gdb_pid
   assert_success
}

# Test with signals
@test "gdb_signal(dynamic): gdb signal support (stray_test)" {
   km_gdb_default_port=2159
   km_with_timeout -g stray_test.kmd signal &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_signal_test.gdb" \
         --ex=c --ex=q stray_test.kmd
   assert_success
   assert_line --partial 'received signal SIGUSR1'
   assert_line --partial 'received signal SIGABRT'
   # check that KM exited normally
   run wait $gdb_pid
   assert_failure 6
}

@test "gdb_exception(dynamic): gdb exception support (stray_test)" {
   km_gdb_default_port=2159
   # Test with signals
   km_with_timeout -g stray_test.kmd stray &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_exception_test.gdb" \
         --ex=c --ex=q stray_test.kmd
   assert_success
   assert_line --partial  'received signal SIGSEGV'
   # check that KM exited normally
   run wait $gdb_pid
   assert_failure 11  # SIGSEGV
}

@test "gdb_server_race(dynamic): gdb server concurrent wakeup test" {
   km_gdb_default_port=2159
   km_trace_file=/tmp/gdb_server_race_test_dynamic_$$.out
   # Test with breakpoints triggering and SIGILL being happending continuously
   # Save output to a log file for our own check using grep below.
   km_with_timeout -V -g gdb_server_entry_race_test.kmd >$km_trace_file 2>&1 &
   gdb_pid=$! ; sleep 0.5
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_default_port" --ex="source cmd_for_gdbserverrace_test.gdb" \
         --ex=c --ex=q gdb_server_entry_race_test.kmd
   assert_success

   # check that KM exited normally
   run wait $gdb_pid
   assert_success

   # look for km trace entries that show the sigill signal overrode the breakpoint
   # when deciding to tell the gdb client why we stopped.
   grep "overriding pending signal" $km_trace_file >/dev/null
   assert_success
   rm -f $km_trace_file
}

@test "gdb_qsupported(dynamic): gdb qsupport/vcont test" {
   km_gdb_default_port=2159
   # Verify that qSupported, vCont?, vCont, and qXfer:threads:read remote
   # commands are being used.
   km_with_timeout -g gdb_qsupported_test.kmd &
   gdb_pid=$!; sleep 0.5
   run gdb_with_timeout -q -nx --ex="set debug remote 1" --ex="target remote :$km_gdb_default_port" \
      --ex="source cmd_for_qsupported_test.gdb" --ex=q gdb_qsupported_test.kmd
   assert_success

   # Verify that km gdb server is responding to a qSupported packet
   assert_line --partial "Packet received: PacketSize=00003FFF;qXfer:threads:read+;swbreak+;hwbreak+;vContSupported+"
   # Verify that km gdb server is responding to a vCont? packet
   assert_line --partial "Packet received: vCont;c;s;C;S"
   # Verify that km gdb server is responding to a qXfer:threads:read" packet
   assert_line --partial "Packet received: m<threads>\\n  <thread id"
   # Verify that the gdb client is using vCont packets
   assert_line --partial "Sending packet: \$vCont;s:"
   # Verify that km gdb server is sending a stop packet with a thread id
   assert_line --partial "Packet received: T05hwbreak:;thread:"
   # Verify that we now see the switching threads message
   assert_line --partial "Switching to Thread 2"

   run wait $gdb_pid
   assert_success
}

@test "Unused memory protection(dynamic): check that unused memory is protected (mprotect_test)" {
   run km_with_timeout mprotect_test.kmd -v
   assert_success
}

@test "threads_basic(dynamic): threads with TLS, create, exit and join (hello_2_loops_tls_test)" {
   run km_with_timeout hello_2_loops_tls_test.kmd
   assert_success
   refute_line --partial 'BAD'
}

@test "threads_basic(dynamic): threads with TSD, create, exit and join (hello_2_loops_test)" {
   run km_with_timeout -Vsignal hello_2_loops_test.kmd
   assert_success
}

@test "threads_exit_grp(dynamic): force exit when threads are in flight (exit_grp_test)" {
   run km_with_timeout exit_grp_test.kmd
   # the test can exit(17) from main thread or random exit(11) from subthread
   assert [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex(dynamic): mutex (mutex_test)" {
   run km_with_timeout mutex_test.kmd
   assert_success
}

@test "mem_test(dynamic): threads create, malloc/free, exit and join (mem_test)" {
   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   run km_with_timeout mem_test.kmd
   assert [ $status -eq $expected_status ]
}

@test "pmem_test(dynamic): test physical memory override (pmem_test)" {
   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 hello_test.kmd
   assert_failure
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 hello_test.kmd
      assert_success
   fi
   run km_with_timeout -P `expr $(bus_width) + 1` hello_test.km # Don't support guest bus larger the host bus
   assert_failure
   run km_with_timeout -P 0 hello_test.kmd # Don't support 0 width bus
   assert_failure
   run km_with_timeout -P -1 hello_test.kmd
   assert_failure
}

@test "brk_map_test(dynamic): test brk and map w/physical memory override (brk_map_test)" {
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 brk_map_test.kmd -- 33
      assert_success
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages brk_map_test.kmd -- 33
   assert_failure
}

@test "cli(dynamic): test 'km -v' and other small tests" {
   skip "Already run in static tests"
   run km_with_timeout -v
   assert_success
   assert_line --partial `git rev-parse --abbrev-ref HEAD`
   assert_line --partial 'Kontain Monitor v'
   run km_with_timeout --version
   assert_success
}

@test "cpuid(dynamic): test cpu vendor id (cpuid_test)" {
   run km_with_timeout cpuid_test.kmd
   assert_success
   assert_line --partial 'Kontain'
}

@test "longjmp_test(dynamic): basic setjmp/longjump" {
   args="more_flags to_check: -f and check --args !"
   run ./longjmp_test $args
   assert_success
   linux_out="${output}"

   run km_with_timeout longjmp_test.kmd $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "exception(dynamic): exceptions and faults in the guest (stray_test)" {
   CORE=/tmp/kmcore.$$
   skip "TODO: gdb tries to use PT_INTERP. Doesn't line __km_dynlink__"
   # divide by zero
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd div0
   assert_failure 8 # SIGFPE
   echo $output | grep -F 'Floating point exception (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'div0 ('
   # Check number of segments. Shoudl be 8
   nload=`readelf -l ${CORE} | grep LOAD | wc -l`
   assert [ "${nload}" == "9" ]
   rm -f ${CORE}

   # invalid opcode
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd ud
   assert_failure 4 # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'undefined_op ('
   rm -f ${CORE}

   # page fault
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd stray
   assert_failure 11 # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # bad hcall
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd hc 400
   assert_failure 31 # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'main ('
   rm -f ${CORE}

   # write to text (protected memory)
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd prot
   assert_failure 11  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'write_text ('
   assert rm -f ${CORE}

   # abort
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd abort
   assert_failure 6  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'abort ('
   rm -f ${CORE}

   # quit
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd quit
   assert_failure 3  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   assert [ -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'kill ('
   rm -f ${CORE}

   # term
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd term
   assert_failure 15  # SIGTERM
   echo $output | grep -F 'Terminated'
   assert [ ! -f ${CORE} ]

   # signal
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd signal
   assert_failure 6  # SIGABRT
   echo $output | grep -F 'Aborted'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'abort ('
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'signal_abort_handler ('
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F '<signal handler called>'
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'signal_abort_test ('
   rm -f ${CORE}

   # sigsegv blocked
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd block-segv
   assert_failure 11  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [  -f ${CORE} ]
   gdb --ex=bt --ex=q stray_test.kmd ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # ensure that the guest can ignore a SIGPIPE.
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test.kmd sigpipe
   assert_success  # should succeed
   assert [ ! -f ${CORE} ]

   # Try to close a KM file from the guest
   run km_with_timeout --coredump=${CORE} stray_test.kmd close 5
   assert_failure 9  # EBADF
   assert [ ! -f ${CORE} ]
}

@test "signals(dynamic): signals in the guest (signals)" {
   run km_with_timeout signal_test.kmd -v
   assert_success
}

@test "pthread_cancel(dynamic): (pthread_cancel_test)" {
   run km_with_timeout pthread_cancel_test.kmd -v
   assert_success
}

# C++ tests
@test "cpp(dynamic): constructors and statics (var_storage_test)" {
   skip "TODO: need to fix"

   run km_with_timeout --putenv LD_LIBRARY_PATH=${KM_LDSO_PATH} var_storage_test.kmd
   assert_success

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]
}


@test "cpp(dynamic): basic throw and unwind (throw_basic_test)" {
   run ./throw_basic_test
   assert_success
   linux_out="${output}"

   run km_with_timeout --putenv LD_LIBRARY_PATH=${KM_LDSO_PATH} throw_basic_test.kmd
   assert_success

   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "filesys(dynamic): guest file system operations (filesys_test)" {
   run km_with_timeout filesys_test.kmd -v
   assert_success
}

@test "filepath(dynamic): guest file path operations (filepathtest)" {
   DIRNAME=`mktemp -d`
   # note: the 2nd parameter (500) is the number o time the
   #       concurrent_open_test runs in a loop. The3 default is
   #       10000. We use 500 here to accomodate azure, where the
   #       open/close cycle is ~40ms vs 1ms on a local workstation.
   run km_with_timeout filepath_test.kmd ${DIRNAME} 500
   assert_success
   rm -rf /tmp/${DIRNAME}
}

@test "socket(dynamic): guest socket operations (socket_test)" {
   run km_with_timeout socket_test.kmd
   assert_success
}

@test "dl_iterate_phdr(dynamic): AUXV and dl_iterate_phdr (dl_iterate_phdr_test)" {
   skip "TODO: need to change count"
   run km_with_timeout dl_iterate_phdr_test.kmd -v
   assert_success
}

@test "monitor_maps(dynamic): munmap gdt and idt (munmap_monitor_maps_test)" {
   skip "TODO: fix counts"
   run km_with_timeout munmap_monitor_maps_test.kmd
   assert_success
}

@test "hypercall args(dynamic): test hcall args passing" {
   run km_with_timeout --overcommit-memory hcallargs_test.kmd
   assert_success
}
