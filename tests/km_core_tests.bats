# Copyright © 2019-2021 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.
#

# km normally tries to avoid sending its message to stderr when stderr is a pipe.
# The bats tests need this behavior so, tell it to keep logging to stderr.
KM_ARGS="--km-log-to=stderr"

signal_flag=128

load test_helper

# Lists of tests to skip (space separated). Wildcards (glob) can be used, but please use '' for the whole list

# not_needed_{generic,static,dynamic,shared} - skip since it's not needed
# todo_{generic,static,dynamic,shared} - skip since it's a TODO
not_needed_generic=''
# TODO: gdb_delete_breakpoint and gdb_server_race are caused by race described in https://github.com/kontainapp/km/issues/821.
# Disable them for now to improve signal/noise ratio
todo_generic='gdb_delete_breakpoint gdb_server_race clock_gettime'

not_needed_static='gdb_sharedlib dlopen'
todo_static=''

# skip slow ones
not_needed_alpine_static='km_main_argv0 km_main_shebang km_main_symlink linux_exec setup_link setup_load gdb_sharedlib mem_regions threads_mutex sigaltstack mem_test readlink_argv km_identity dlopen exec_sh'
# review - some fail. Some slow
todo_alpine_static='dl_iterate_phdr gdb_forkexec'

# glibc native
not_needed_glibc_static='setup_link setup_load gdb_sharedlib readlink_argv km_identity dlopen exec_sh'

# exception - extra segment in kmcore
# dl_iterate_phdr - load starts at 4MB instead of 2MB
# filesys - dup3 flags check inconsistency between musl and glibc
# gdb_nextstep - uses clone_test, same as raw_clone
# raw_clone - glibc clone() wrapper needs pthread structure
# gdb_forkexec - gdb stack trace needs symbols when in a hypercall

todo_glibc_static='exception dl_iterate_phdr filesys gdb_nextstep raw_clone xstate_test  threads_basic_tsd threads_exit_grp gdb_forkexec km_exec_guest_files'

not_needed_alpine_dynamic=$not_needed_alpine_static
todo_alpine_dynamic=$todo_alpine_static

# note: these are generally redundant as they are tested in 'static' pass
not_needed_dynamic='km_main_argv0 km_main_shebang km_main_symlink linux_exec setup_load mem_slots cli km_main_env mem_brk mmap_1 readlink_argv km_identity exec_sh'
todo_dynamic='mem_mmap exception dl_iterate_phdr monitor_maps km_exec_guest_files'

# running .so as executables was useful at some point, but it isn't needed anymore.
# Simply disable the tests for now. Ultimately we will drop build and test support for them.
todo_so=''
not_needed_so='*'

# make sure it does not leak in from the outer shell, it can mess out the output
unset KM_VERBOSE

# exclude more tests for Kontain Kernel Module (leading space *is* needed)
if [ "${USE_VIRT}" = 'kkm' ]; then
   not_needed_alpine_dynamic=$not_needed_alpine_static
fi

if [ "${USE_VIRT}" = 'kvm' ]; then
   todo_generic+=' '
fi

# Now the actual tests.
#
# They can be invoked by either 'make test [MATCH=<filter>]' or ./run_bats_tests.sh [--match=filter]
# <filter> is a regexp or substring matching test name
#
# Test string should be "name_with_no_spaces($test_type) description (file_name$ext)"

# Note about port usage:
# In order to run tests in parallel, each test which needs a port is configured to use a UNIQUE port.
# $port_range_start indicates the beginning of the range.
# Due to bats implementation peculiarities, it's hard to automate port number assignment so each test
# manually defines a unique (for this .bats file) port id (offset within the range),
# and uses port=$(( $port_range_start + $port_id))

@test "hypervisor($test_type) Check access to /dev/${USE_VIRT}" {
   assert [ -c /dev/${USE_VIRT} ]
}

@test "linux_exec($test_type) make sure *some* linux tests actually pass" {
   # Note: needed only once, expected to run only in static pass
   # TODO actual run. MANY TESTS FAILS - need to review. Putting in a scaffolding hack for now
   for test in hello mmap_1 mem env misc mutex longjmp memslot mprotect semaphore; do
      echo Running ${test}_test.fedora
      ./${test}_test.fedora
   done
}

@test "setup_link($test_type): check if linking produced text segment where we expect (load_test$ext)" {
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
   run km_with_timeout load_test$ext
   # Show this on failure:
   echo -e "\n*** Try to run 'make load_expected_size' in tests, and replace load.c:size value\n"
   assert_success
}

@test "hc_check($test_type): invoke wrong hypercall (hc_test$ext)" {
   run km_with_timeout stray_test$ext hc 400
   assert_failure $(( $signal_flag + 31))   #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test$ext -- hc -10
   assert_failure $(( $signal_flag + 7))   #SIGBUS
   assert_output --partial "Bus error"

   run km_with_timeout stray_test$ext hc 1000
   assert_failure $(( $signal_flag + 7))   #SIGBUS
   assert_output --partial "Bus error"

   run km_with_timeout stray_test$ext hc-badarg 3
   assert_failure $(( $signal_flag + 11))  #SIGSEGV
   assert_output --partial "Segmentation fault"

   run km_with_timeout stray_test$ext syscall
   assert_success
   assert_output --partial "Hello from SYSCALL"
}

@test "km_main_signal($test_type): wait on signal (hello_test$ext)" {
   run timeout -s SIGUSR1 1s ${KM_BIN} --km-log-to=stderr --dynlinker=${KM_LDSO} --wait-for-signal hello_test$ext
   assert_failure 124
}

@test "km_main_args($test_type): optargs (hello_test$ext)" {
   set +x
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
   assert_line --partial "Invalid gdb port number 'foobar'"

   corefile=/tmp/km$$
   run km_with_timeout -Vcoredump -C $corefile -- hello_test$ext # -C sets coredump file name
   assert_success
   assert_output --partial "Setting coredump path to $corefile"

   run km_with_timeout -P 31 -- hello_test$ext # -P sets Physical memory bus width
   assert_failure
   assert_line --partial "Guest memory bus width must be between 32 and 63 - got '31'"

   run km_with_timeout -P32 -- hello_test$ext
   check_optional_mem_size_failure

   run km_with_timeout -X # invalid option
   assert_failure
   assert_line --partial "invalid option"

   log=`mktemp`
   echo Log location: $log
   run km_with_timeout -V --log-to=$log -- hello_test$ext # check --log-to option
   assert_success
   assert [ -e $log ]
   assert grep -q 'Hello, world' $log       # guest stdout redirected by --log-to
   assert_output --partial 'Setting VendorId ' $log  # km stderr
   rm $log
   run km_with_timeout -V --log-to=/very/bad/place -- hello_test$ext
   assert_failure
}

@test "km_main_argv0($test_type): redirecting argv0 to argv0.km payload messages (hello_test$ext)" {
   # test that KM redirects to proper payload.km when invoked as `./payload`
   local payload=hello_test
   KM_VERBOSE=generic run ./$payload SomeArg --LooksLikeAFlag
   assert_success
   assert_line --partial "argv[1] = 'SomeArg'"
   refute_line "invalid option"
   KMLOGFILE=`echo -e ${output} | grep "Switch km logging to" | sed -e "s/.*Switch km logging to //" | sed -e "s/ on first attempt to log.*//"`
   grep -q "Setting payload name to .*/$payload.km" $KMLOGFILE
   assert_success
   run grep -q "invalid option" $KMLOGFILE
   assert_failure
}

@test "km_main_env($test_type): passing environment to payloads (env_test$ext)" {
   val=`pwd`/$$

   # --putenv defines an env var and cancels host env
   run km_with_timeout --putenv PATH=$val env_test$ext
   assert_success
   assert_output --partial "PATH=$val"
   refute_output --partial "getenv: PATH=$PATH"

   # by default, host env is used
   run km_with_timeout  env_test$ext
   assert_success
   assert_line "getenv: PATH=$PATH"
}

@test "mem_slots($test_type): KVM memslot / phys mem sizes (memslot_test$ext)" {
   run ./memslot_test
   assert_success
}

@test "mem_regions($test_type): Crossing regions boundary (regions_test$ext)" {
   run ${KM_BIN} ${KM_ARGS} regions_test$ext
   assert_success
}

@test "mem_brk($test_type): brk() call (brk_test$ext)" {
   # we expect 3 group of tests to fail due to ENOMEM on 36 bit/no_1g hardware
   if [ $(bus_width) -eq 36 ] ; then expected_status=3 ; else  expected_status=0; fi
   run km_with_timeout --overcommit-memory brk_test$ext
   assert [ $status -eq $expected_status ]
}

@test "hc_basic($test_type): basic run and print hello world (hello_test$ext)" {
   args="more_flags to_check: -f and check --args !"
   run ./hello_test.fedora $args
   assert_success
   linux_out="${output}"

   run km_with_timeout hello_test$ext $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "hc_socket($test_type): basic HTTP/socket I/O (hello_html_test$ext)" {
   local port_id=0
   local port=$(( $port_range_start + $port_id))
   local address="http://127.0.0.1:$port"

   (./hello_html_test.fedora $port &)
   run curl -s $address --retry-connrefused  --retry 3 --retry-delay 1
   assert_success
   linux_out="${output}"

   (km_with_timeout hello_html_test$ext $port &)
   run curl -s $address --retry-connrefused  --retry 3 --retry-delay 1
   assert_success
   diff <(echo -e "$linux_out") <(echo -e "$output")
}

# placeholder for multiple small tests... we can put them all in misc_test.c
@test "misc_tests($test_type): Misc APIs, i.e. uname (misc_test$ext)" {
   run km_with_timeout misc_test$ext
   assert_success
   assert_line "nodename=$(uname -n)"
   assert_line sysname=kontain-runtime
   assert_line machine=kontain_${USE_VIRT^^}
}

@test "mem_mmap($test_type): mmap and munmap with addr=0 (mmap_test$ext)" {
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; else  expected_status=0; fi

   run gdb_with_timeout --ex="source gdb_simple_test.py" --ex="handle SIG63 nostop" \
       --ex="run-test" --ex="q" --args ${KM_BIN} mmap_test$ext -v
   assert [ $status -eq $expected_status ]
   assert_line --partial 'fail: 0'

   # check the failure codes on linux
   run ./mmap_test.fedora -v -t mmap_file_test_ex
   assert_line --partial 'fail: 0'

   # make sure there is a filename somewhere in the maps
   run gdb_with_timeout -ex="set radix 0xa" -ex="set pagination off" -ex="handle SIG63 nostop" \
      -ex="source gdb_simple_test.py" -ex="run-test" \
      -ex="q" --args ${KM_BIN} mmap_test$ext -v -t mmap_file_test_ex # KM test
   assert_line --partial 'fail: 0'
   assert_line --regexp 'prot=1 flags=2 .* fn=0x'
}

@test "mmap_1($test_type): mmap then smaller mprotect (mmap_1_test$ext)" {
   run gdb_with_timeout --ex="set pagination off" --ex="handle SIG63 nostop" \
      --ex="source gdb_simple_test.py" --ex="run-test" --ex="q" --args ${KM_BIN} mmap_1_test$ext
   assert_line --partial 'fail: 0'
}

@test "futex($test_type): basic futex operations" {
   run km_with_timeout futex_test$ext
   assert_success
}

@test "gdb_basic($test_type): gdb support (gdb_test$ext)" {
   local port_id=2
   local km_gdb_port=$(( $port_range_start + $port_id))
   # start KM in background, give it time to start, and connect with gdb client
   km_with_timeout -g$km_gdb_port gdb_test$ext &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex="source cmd_for_test.gdb" \
         --ex=c --ex=q gdb_test$ext
   # check that gdb found what it is supposed to find
   assert_line --partial 'SUCCESS'
   wait_and_check $pid 0 # expect KM to exit normally
}

# Test with signals
@test "gdb_signal($test_type): gdb signal support (stray_test$ext)" {
   local port_id=3
   local km_gdb_port=$(( $port_range_start + $port_id))
   km_with_timeout -g$km_gdb_port stray_test$ext signal &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex="source cmd_for_signal_test.gdb" \
         --ex=c --ex=q stray_test$ext
   assert_success
   assert_line --partial 'received signal SIGUSR1'
   assert_line --partial 'received signal SIGABRT'
   wait_and_check $pid $(( $signal_flag + 6)) # expect payload to get SIGABRT
}

@test "gdb_exception($test_type): gdb exception support (stray_test$ext)" {
   local port_id=4
   local km_gdb_port=$(( $port_range_start + $port_id))
   # Test with signals
   km_with_timeout -g$km_gdb_port stray_test$ext stray &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex="source cmd_for_exception_test.gdb" \
         --ex=c --ex=q stray_test$ext
   assert_success
   assert_line --partial  'received signal SIGSEGV'
   wait_and_check $pid $(( $signal_flag + 11)) # expect KM to exit with SIGSEGV
}

@test "gdb_server_race($test_type): gdb server concurrent wakeup test" {
   local port_id=5
   local km_gdb_port=$(( $port_range_start + $port_id))
   km_trace_file=/tmp/gdb_server_race_test_static_$$.out
   # Test with breakpoints triggering and SIGILL happening continuously
   # Save output to a log file for our own check using grep below.
   echo trace in $km_trace_file
   km_with_timeout -V -g$km_gdb_port gdb_server_entry_race_test$ext >$km_trace_file 2>&1 &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex="source cmd_for_gdbserverrace_test.gdb" \
         --ex=c --ex=q gdb_server_entry_race_test$ext
   assert_success # check gdb exit $status
   # wait for KM to exit
   wait $pid
   status=$?
   if [ $status -ne 0 ] ; then
      file_contents_to_bats_log $km_trace_file $status
   else
      rm -f $km_trace_file
   fi
   assert_success  # check KM exit $status
}

@test "gdb_qsupported($test_type): gdb qsupport/vcont test" {
   local port_id=6
   local km_gdb_port=$(( $port_range_start + $port_id))
   # Verify that qSupported, vCont?, vCont, and qXfer:threads:read remote
   # commands are being used.
   km_with_timeout -g$km_gdb_port gdb_qsupported_test$ext &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="set debug remote 1" --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_qsupported_test.gdb" --ex=q gdb_qsupported_test$ext
   assert_success

   # Verify that km gdb server is responding to a qSupported packet
   assert_line --partial "Packet received: PacketSize=00003FFF;qXfer:threads:read+;swbreak+;hwbreak+;exec-events+;vContSupported+"
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
   wait_and_check $pid 0 # expect KM to exit normally
}

@test "gdb_delete_breakpoint($test_type): gdb delete breakpoint test" {
   local port_id=7
   local km_gdb_port=$(( $port_range_start + $port_id))
   km_trace_file=/tmp/gdb_delete_breakpoint_test_$$.out

   km_with_timeout -V -g$km_gdb_port gdb_delete_breakpoint_test$ext >$km_trace_file 2>&1 &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_delete_breakpoint_test.gdb" --ex=q gdb_delete_breakpoint_test$ext
   assert_success
   assert grep -q "Deleted breakpoint, discard event:" $km_trace_file
   wait_and_check $pid 0 # expect KM to exit normally
   # These grep's are useful when we start seeing failures of this test.
   # They let us see how many interations the test is going through
   # and how often the race is being seen.
   # On idle personal test systems we see about 300-500 instances of
   # the race during 700 iterations.  Let's keep these commented greps around
   # for a while.
   #grep "Deleted break" $km_trace_file | wc -l >/dev/tty
   #grep "iterations" $km_trace_file >/dev/tty
   rm -fr $km_trace_file
}

@test "gdb_nextstep($test_type): gdb next step test" {
   local port_id=8
   local km_gdb_port=$(( $port_range_start + $port_id))

   km_with_timeout -g$km_gdb_port gdb_nextstep_test$ext &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_nextstep_test.gdb" --ex=q gdb_nextstep_test$ext
   assert_success

   # Verify we "next"ed thru next_thru_this_function()
   refute_line --partial "next_thru_this_function () at gdb_nextstep_test.c"
   # Verify we "step"ed into step_into_this_function()
   assert_line --partial "step_into_this_function () at gdb_nextstep_test.c"
   wait_and_check $pid 0 # expect KM to exit normally

   km_with_timeout -g$km_gdb_port clone_test$ext &
   pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_nextclone_test.gdb" --ex=q clone_test$ext
   assert_success

   # Verify we "next"ed thru clone()
   assert_line --regexp "^#0  main.* at clone_test.c:38$"
   wait_and_check $pid 0 # expect KM to exit normally
}

#
# Tests to exercise gdb remote protocol requests:
#   qXfer:auxv:read
#   qXfer:exec-file:read
#   qXfer:libraries-svr4:read
#   vFile:{open,pread,close,fstat,setfs}  readlink is supported but not yet tested.
# Also test the new -G flag to build/km/km.  This allows us to attach gdb to km before
# the dynamic linker starts. -g attaches at the _start entry point.
# We test qXfer:exec-file:read by not supplying the name of the executable on the
# gdb command line.  This forces the client to query for the executable file name.
# The executable we run for this test is not very important.
#
@test "gdb_sharedlib($test_type): gdb shared libary related remote commands" {
   local port_id=9
   local km_gdb_port=$(( $port_range_start + $port_id))

   # test with attach at dynamic linker entry point
   km_with_timeout -g$km_gdb_port --gdb-dynlink stray_test$ext stray &
   local pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_sharedlib_test.gdb" --ex=q
   assert_success
   refute_line --regexp "0x[0-9a-f]* in _start ()"
   wait_and_check $pid $(( $signal_flag + 11)) # expect KM to abort with SIGSEGV

   # test with attach at _start entry point
   km_with_timeout -g$km_gdb_port stray_test$ext stray &
   pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_sharedlib_test.gdb" --ex=q
   assert_success
   assert_line --regexp "0x[0-9a-f]* in _start ()"
   # Check for some output from "info auxv"
   assert_line --regexp "31   AT_EXECFN            File name of executable *0x[0-9a-f]*.*stray_test.kmd"
   # Check for some output from "info sharedlibrary"
   assert_line --regexp "0x[0-9a-f]*  0x[0-9a-f]*  Yes         target:.*libc.so"
   wait_and_check $pid $(( $signal_flag + 11)) # expect KM to exit with SIGSEGV

   # There is no explicit test for vFile remote commands.  gdb uses vFile as part of
   # processing the "info sharedlibrary" command.  But we do need to use the gdb "info proc"
   # command to try out vfile readlink.
   km_with_timeout -g$km_gdb_port stray_test$ext stray &
   pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex="info proc $pid" -ex=cont -ex=q
   assert_success
   assert_line --partial "exe = '"
   wait_and_check $pid $(( $signal_flag + 11)) # expect KM to exit with SIGSEGV

   # test for symbols from a shared library brought in by dlopen()
   km_with_timeout -g$km_gdb_port --putenv="LD_LIBRARY_PATH=`pwd`" gdb_sharedlib2_test$ext &
   pid=$!
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_sharedlib2_test.gdb" --ex=q
   assert_success
   assert_line --regexp "Yes         target:.*/dlopen_test_lib.so"
   assert_line --partial "Dump of assembler code for function do_function"
   assert_line --partial "Hit the breakpoint at do_function"
   wait_and_check $pid 0
}

#
# Verify that gdb client can attach to a running km and payload.
# Then detach and then attach and detach again.
# Then finally attach one more time to shut the test down.
#
@test "gdb_attach($test_type): gdb client attach test (gdb_lots_of_threads_test$ext)" {
   local port_id=10
   local km_gdb_port=$(( $port_range_start + $port_id))

   # test asynch gdb client attach to the target
   km_with_timeout -g$km_gdb_port --gdb-listen gdb_lots_of_threads_test$ext &
   local pid=$!
   run gdb_with_timeout -q -nx \
      --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_attach_test.gdb" --ex=q
   assert_success
   assert_line --partial "Thread 6 \"vcpu-5\""
   # TODO: remove the check with glibc_static when stack trace is implemented
   [[ $test_type =~ (alpine|glibc)* ]] || assert_line --partial "in do_nothing_thread (instance"
   assert_line --partial "Inferior 1 (Remote target) detached"

   # 2nd try to test async gdb client attach to the target
   run gdb_with_timeout -q -nx \
      --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_attach_test.gdb" --ex=q
   assert_success
   assert_line --partial "Thread 8 \"vcpu-7\""
   # TODO: remove the check with glibc_static when stack trace is implemented
   [[ $test_type =~ (alpine|glibc)* ]] || assert_line --partial "in do_nothing_thread (instance"
   assert_line --partial "Inferior 1 (Remote target) detached"

   # ok, gdb client attach seems to be working, shut the test program down.
   run gdb_with_timeout -q -nx \
      --ex="target remote :$km_gdb_port" \
      --ex="set stop_running=1" \
      --ex="source cmd_for_attach_test.gdb" --ex=q
   assert_success
   assert_line --partial "Inferior 1 (Remote target) detached"

   wait_and_check $pid 0

   # Try to attach to a payload where gdb server is not listening.
   # Leave this test commented out since the gdb client connect timeout
   # is about 15 seconds which is too long for CI testing.
   #km_with_timeout gdb_lots_of_threads_test$ext -a 1 &
   #pid=$!
   #run gdb_with_timeout -q --ex="target remote :$km_gdb_port" --ex=q
   #assert_line --partial "Connection timed out"
   #wait_and_check $pid 0
}

#
# Verify that gdb can read and write pages that disallow read and write
#
@test "gdb_protected_mem($test_type): gdb access protected memory test (gdb_protected_mem_test$ext)" {
   local port_id=11
   local km_gdb_port=$(( $port_range_start + $port_id))

   # test gdb can read from and write to protected memory pages.
   km_with_timeout -g$km_gdb_port gdb_protected_mem_test$ext &
   local pid=$!
   run gdb_with_timeout -q -nx \
      --ex="target remote :$km_gdb_port" \
      --ex="source cmd_for_protected_mem_test.gdb" --ex=q
   assert_success
   assert_line --partial "first word  0x7fffffbfc000:	0x1111111111111111"
   assert_line --partial "spanning pages  0x7fffffbfcffc:	0xff0000ffff0000ff"
   assert_line --partial "last word  0x7fffffbfdff8:	0xeeeeeeeeeeeeeeee"
   wait_and_check $pid 0
}

@test "unused_memory_protection($test_type): check that unused memory is protected (mprotect_test$ext)" {
   run km_with_timeout mprotect_test$ext -v
   assert_success
}

@test "madvise_memory_test($test_type): basic madvice manipulations (madvise_test$ext)" {
   run km_with_timeout madvise_test$ext -v
   assert_success
}

@test "threads_basic($test_type): threads with TLS, create, exit and join (hello_2_loops_tls_test$ext)" {
   run km_with_timeout hello_2_loops_tls_test.km
   assert_success
   if [ $test_type != "static" ] ; then
      refute_line --partial 'BAD'
   fi
}

@test "threads_basic_tsd($test_type): threads with TSD, create, exit and join (hello_2_loops_test$ext)" {
   for i in $(seq 1 3); do # only fail if all 3 tries failed
      echo pass $i
      run km_with_timeout hello_2_loops_test$ext
      if [ $status == 0 ] ; then break; fi
   done
   assert_success
}

@test "threads_exit_grp($test_type): force exit when threads are in flight (exit_grp_test$ext)" {
   run km_with_timeout exit_grp_test$ext
   # the test can exit(17) from main thread or random exit(11) from subthread
   assert [ $status -eq 17 -o $status -eq 11  ]
}

@test "threads_mutex($test_type): mutex (mutex_test$ext)" {
   run km_with_timeout mutex_test$ext
   assert_success
}

@test "mem_test($test_type): threads create, malloc/free, exit and join (mem_test$ext)" {
   expected_status=0
   # we expect 1 group of tests fail due to ENOMEM on 36 bit buses
   if [ $(bus_width) -eq 36 ] ; then expected_status=1 ; fi
   run km_with_timeout mem_test$ext
   assert [ $status -eq $expected_status ]
}

@test "pmem_test($test_type): test physical memory override (hello_test$ext)" {
   # Don't support bus smaller than 32 bits
   run km_with_timeout -P 31 hello_test$ext
   assert_failure
   # run hello test in a guest with a 33 bit memory bus.
   # TODO: instead of bus_width, we should look at pdpe1g - without 1g pages, we only support 2GB of memory anyways
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P 33 hello_test$ext
      check_optional_mem_size_failure
   fi
   run km_with_timeout -P `expr $(bus_width) + 1` hello_test$ext # Don't support guest bus larger the host bus
   assert_failure
   run km_with_timeout -P 0 hello_test$ext # Don't support 0 width bus
   assert_failure
   run km_with_timeout -P -1 hello_test$ext
   assert_failure
}

@test "brk_map_test($test_type): test brk and map w/physical memory override (brk_map_test$ext)" {
   if [ $(bus_width) -gt 36 ] ; then
      run km_with_timeout -P33 brk_map_test$ext -- 33
      check_optional_mem_size_failure
   fi
   # make sure we fail gracefully if there is no 1G pages supported. Also checks longopt
   run km_with_timeout --membus-width=33 --disable-1g-pages brk_map_test$ext -- 33
   assert_failure
}

@test "cli($test_type): test 'km -v' and other small tests" {
   run km_with_timeout -v
   assert_success
   assert_line --partial `git rev-parse --abbrev-ref HEAD`
   assert_line --partial 'Kontain Monitor v'
   run km_with_timeout --version
   assert_success
}

@test "cpuid($test_type): test cpu vendor id (cpuid_test$ext)" {
   cpuidexpected='Kontain'
   if [ "${USE_VIRT}" = 'kkm' ]; then
      cpuidexpected='GenuineIntel'
   fi
   run km_with_timeout cpuid_test$ext
   assert_success
   assert_line --partial $cpuidexpected
}

@test "longjmp_test($test_type): basic setjmp/longjump" {
   args="more_flags to_check: -f and check --args !"
   run ./longjmp_test.fedora $args
   assert_success
   linux_out="${output}"

   run km_with_timeout longjmp_test$ext $args
   assert_success
   # argv[0] differs for linux and km (KM argv[0] is different, and there can be 'km:  .. text...' warnings) so strip it out, and then compare results
   diff <(echo -e "$linux_out" | grep -F -v 'argv[0]') <(echo -e "$output" | grep -F -v 'argv[0]' | grep -v '^km:')
}

@test "exception($test_type): exceptions and faults in the guest (stray_test$ext)" {
   CORE=/tmp/kmcore.$$
   # divide by zero
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext div0
   assert_failure $(( $signal_flag + 8)) # SIGFPE
   echo $output | grep -F 'Floating point exception (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'div0 ('
   # Check number of segments. Shoudl be 8
   nload=`readelf -l ${CORE} | grep LOAD | wc -l`
   assert [ "${nload}" == "12" ]
   rm -f ${CORE}

   # invalid opcode
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext ud
   assert_failure $(( $signal_flag + 4)) # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'undefined_op ('
   rm -f ${CORE}

   # page fault
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext stray
   assert_failure $(( $signal_flag + 11)) # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # bad hcall
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext hc 400
   assert_failure $(( $signal_flag + 31)) # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'main ('
   rm -f ${CORE}

   # write to text (protected memory)
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext prot
   assert_failure $(( $signal_flag + 11))  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'write_text ('
   assert rm -f ${CORE}

   # abort
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext abort
   assert_failure $(( $signal_flag + 6))  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'abort ('
   rm -f ${CORE}

   # quit
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext quit
   assert_failure $(( $signal_flag + 3))  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'kill ('
   rm -f ${CORE}

   # term
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext term
   assert_failure $(( $signal_flag + 15))  # SIGTERM
   echo $output | grep -F 'Terminated'
   assert [ ! -f ${CORE} ]

   # signal
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext signal
   assert_failure $(( $signal_flag + 6))  # SIGABRT
   echo $output | grep -F 'Aborted'
   assert [  -f ${CORE} ]
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'abort ('
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'signal_abort_handler ('
   # With km_sigreturn in km itself as opposed to libruntine, stack
   # traces going across a signal handler don't work very well.
   #gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F '<signal handler called>'
   #gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'signal_abort_test ('
   rm -f ${CORE}

   # sigsegv blocked
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext block-segv
   assert_failure $(( $signal_flag + 11))  # SIGSEGV
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

   # Execute a HLT instructuction
   run km_with_timeout --coredump=${CORE} stray_test$ext halt
   assert_failure $(( $signal_flag + 11))  # SIGSEGV
   assert [ -f ${CORE} ]
   rm -f ${CORE}
}

@test "signals($test_type): signals in the guest (signals)" {
   run km_with_timeout -Vhyper signal_test$ext -v
   assert_success
   assert_line --partial "Ignoring tgid 100"

   # Try out the sigtimedwait() hypercall
   run km_with_timeout sigtimedwait_test$ext
}

@test "pthread_cancel($test_type): (pthread_cancel_test$ext)" {
   for i in $(seq 1 3); do # only fail if all 3 tries failed
      echo pass $i
      run km_with_timeout pthread_cancel_test$ext -v
      if [[ $status == 0 && \
         ! "$output" =~ "PTHREAD_CANCEL_ASYNCHRONOUS" && \
         "$output" =~ "PTHREAD_CANCEL_DEFERRED" &&
         "$output" =~ "thread_func(): end of DISABLE_CANCEL_TEST" ]]
      then break; else status=1; fi
   done
   assert_success
   # This is what we really need to check once, once pthread_cancel stops being noisy
   # assert_line --partial "thread_func(): end of DISABLE_CANCEL_TEST"
   # refute_line --partial "PTHREAD_CANCEL_ASYNCHRONOUS"
   # assert_line --partial "PTHREAD_CANCEL_DEFERRED"
}

# C++ tests
@test "cpp_ctors($test_type): constructors and statics (var_storage_test$ext)" {
   run km_with_timeout var_storage_test$ext
   assert_success
   assert_line --partial "ONCE ONCE may_throw_function"

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   onces=`echo -e "$output" | grep -F "ONCE ONCE may_throw_function" | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]
   assert [ "$onces" -eq 1 ]
}

# C++ shared library open from C with dlopen - only makes sense for kmd tests
@test "dlopen($test_type): dlopen_test.kmd will open var_storage.km.so" {
   run km_with_timeout var_storage_test$ext
   assert_success
   assert_line --partial "ONCE ONCE may_throw_function"

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   onces=`echo -e "$output" | grep -F "ONCE ONCE may_throw_function" | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]
   assert [ "$onces" -eq 1 ]
}

@test "cpp_throw($test_type): basic throw and unwind (throw_basic_test$ext)" {
   run ./throw_basic_test.fedora
   assert_success
   linux_out="${output}"

   run km_with_timeout throw_basic_test$ext
   assert_success

   diff <(echo -e "$linux_out")  <(echo -e "$output")
}

@test "filesys($test_type): guest file system operations (filesys_test$ext)" {
   run km_with_timeout filesys_test$ext -v
   assert_success

   # stdout redirect test is part of stray_test
   run km_with_timeout stray_test$ext redir_std
   assert_success
   refute_line --partial "From redir"
   assert_output --partial "After restore"
}

@test "filepath($test_type): guest file path operations (filepathtest$ext)" {
   DIRNAME=`mktemp -d`
   # note: the 2nd parameter (500) is the number o time the
   #       concurrent_open_test runs in a loop. The3 default is
   #       10000. We use 500 here to accomodate azure, where the
   #       open/close cycle is ~40ms vs 1ms on a local workstation.
   run km_with_timeout filepath_test$ext ${DIRNAME} 500
   assert_success
   rm -rf /tmp/${DIRNAME}
}

@test "sigpipe($test_type): sigpipe delivery (sigpipe_test$ext)" {
   run km_with_timeout sigpipe_test$ext
   assert_success
}

@test "socket($test_type): guest socket operations (socket_test$ext)" {
   # This test uses 3 ports, the next free one is 15
   local port_id=12
   local socket_test_port=$(( $port_range_start + $port_id))
   SOCKET_PORT=$socket_test_port run km_with_timeout socket_test$ext
   assert_success
}

@test "dl_iterate_phdr($test_type): AUXV and dl_iterate_phdr (dl_iterate_phdr_test$ext)" {
   run km_with_timeout dl_iterate_phdr_test$ext -v
   assert_success
}

@test "monitor_maps($test_type): munmap gdt and idt (munmap_monitor_maps_test$ext)" {
   run gdb_with_timeout -ex="set pagination off" -ex="handle SIG63 nostop"\
      -ex="source gdb_simple_test.py" -ex="run-test" -ex="q" --args ${KM_BIN} ${KM_ARGS} munmap_monitor_maps_test$ext
   assert_success
   assert_line --partial "conflicts with monitor region 0x7fffffdfe000 size 0x2000"
   assert_line --partial 'fail: 0'
}

@test "hypercall_args($test_type): test hcall args passing" {
   run km_with_timeout --overcommit-memory hcallargs_test$ext
   assert_success
}

@test "decode($test_type): test KM EFAULT decode" {
   run km_with_timeout decode_test$ext
   assert_success
}

@test "vdso($test_type): use function verion of some syscalls (vdso_test$ext)" {
   run km_with_timeout -S vdso_test$ext
   assert_success
   refute_line --partial "auxv[AT_SYSINFO_EHDR] not available"
   refute_line --partial "clock_gettime(228) called"
   refute_line --regexp "getcpu(309) called"
}

@test "syscall($test_type): test SYSCALL instruction emulation" {
   run km_with_timeout stray_test$ext syscall
   assert_success
   assert_output --partial "Hello from SYSCALL"

}

@test "auxv_test($test_type): validate km aux vector (auxv_test$ext)" {
   run km_with_timeout auxv_test$ext
   assert_success
}

# Test fix for issue #459 - short write in core dump
# checks that coredump writing completed sucessfully.
@test "trunc_mmap($test_type): truncate a mmaped file and dump core (trunc_mmap$ext)" {
   CORE=/tmp/kmcore.$$
   FILE=/tmp/trunc_mmap.$$
   run km_with_timeout --coredump=${CORE} stray_test$ext trunc_mmap ${FILE}
   assert_failure $(( $signal_flag + 6))  # SIGABRT
   assert_output --partial "Aborted (core dumped)"
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   rm -f ${FILE} ${CORE}
}

@test "raw_clone($test_type): raw clone syscall (clone_test$ext)" {
   run km_with_timeout clone_test$ext
   assert_success
   assert_output --partial "Hello from clone"
}

@test "sigaltstack($test_type): sigaltstack syscall (sigaltstack_test$ext)" {
   run km_with_timeout sigaltstack_test$ext
   assert_success
}

# Test the sigsuspend() system call
# And verify that signals sent to km are forwarded to the payload
@test "sigsuspend($test_type): sigsuspend() and signal forwarding (sigsuspend_test$ext)" {
   FLAGFILE=/tmp/sigsuspend_test.$$
   KMTRACE=/tmp/sigsuspend_kmtrace.$$
   WAIT=5
   KMOUT=/tmp/sigsuspend_kmout.$$

   rm -f $FLAGFILE $KMTRACE
   $KM_BIN -V sigsuspend_test$ext $FLAGFILE >$KMOUT 2>$KMTRACE &
   pid=$!
   start=`date +%s`
   while [ ! -e $FLAGFILE ]
   do
      sleep .1
      now=`date +%s`
      if test `expr $now - $start` -ge $WAIT
      then
         # Put the km trace in the TAP stream
         file_contents_to_bats_log $KMTRACE
         fail "$FLAGFILE does not exist after $WAIT seconds!"
      fi
   done
   kill -SIGUSR1 $pid
   kill -SIGUSR1 $pid
   kill -SIGUSR2 $pid
   wait $pid
   rm -f $FLAGFILE

   # Be sure the signal handlers were entered
   grep -q "SIGUSR1 signal handler entered" $KMOUT
   grep -q "SIGUSR2 signal handler entered" $KMOUT
   # SIGUSR2 sig handler must be entered before sigsuspend returns
   grep -v SIGUSR1 $KMOUT | tail -1 | grep -q "sigsuspend returned"

   rm -f $KMTRACE $KMOUT
}

@test "itimer($test_type): test setitimer() getitimer() hypercalls (itimer_test$ext)" {
   run km_with_timeout itimer_test$ext
   assert_success
}

@test "basic_snapshot($test_type): snapshot and resume(snapshot_test$ext)" {
   SNAP=/tmp/snap.$$
   CORE=/tmp/core.$$
   KMLOG=/tmp/kmlog.$$
   SNAP_INPUT=/tmp/snap_input.$$
   SNAP_OUTPUT=/tmp/snap_output.$$

   echo "This is test data" > ${SNAP_INPUT}

   for i in $(seq 100) ; do
      # snapshot resume that successfully exits
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext
      assert_success
      assert [ -f ${SNAP} ]
      assert [ ! -f ${CORE} ]
      check_kmcore ${SNAP}
      run km_with_timeout --km-log-to=${KMLOG} --input-data=${SNAP_INPUT} --output-data=${SNAP_OUTPUT} ${SNAP}
      assert_success
      assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test application' ${KMLOG}
      assert [ ! -f ${CORE} ]
      assert [ -f ${SNAP_OUTPUT} ]
      run diff ${SNAP_INPUT} ${SNAP_OUTPUT}
      assert_success
      rm -f ${SNAP} ${KMLOG} ${SNAP_OUTPUT}

      # snapshot with closed stdio
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext -c
      assert_success
      assert [ -f ${SNAP} ]
      assert [ ! -f ${CORE} ]
      check_kmcore ${SNAP}
      run km_with_timeout --km-log-to=${KMLOG} ${SNAP}
      assert_success
      # TODO: remove the check with glibc_static when musl and glibc behave the same way
      [[ $test_type =~ glibc* ]] && refute_line --partial "Hello from thread"
      [[ $test_type =~ glibc* ]] || assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test application' ${KMLOG}
      assert [ ! -f ${CORE} ]
      rm -f ${SNAP} ${KMLOG} ${SNAP_OUTPUT}

      # snapshot resume that core dumps
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext -a
      assert_success
      assert [ -f ${SNAP} ]
      check_kmcore ${SNAP}
      run km_with_timeout --coredump=${CORE} --km-log-to=${KMLOG} ${SNAP}
      assert_failure $(( $signal_flag + 6))  # SIGABRT
      assert [ -f ${CORE} ]
      assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test application' ${KMLOG}
      if [ "$test_type" = ".km.so" ]; then
         gdb --ex=bt --ex=q snapshot_test$ext ${CORE} | grep -F 'abort ('
      fi
      rm -f ${SNAP} ${CORE} ${KMLOG} ${SNAP_OUTPUT}

      # 'live' snapshot
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext -l
      assert_success
      assert [ -f ${SNAP} ]
      check_kmcore ${SNAP}
      assert_output --partial "Hello from thread"
      run km_with_timeout --km-log-to=${KMLOG} ${SNAP}
      assert_success
      assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test application' ${KMLOG}
      assert [ ! -f ${CORE} ]
      rm -f ${SNAP} ${KMLOG} ${SNAP_OUTPUT}
   done

   # make sure resume with payloads args fails
   run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext
   assert_success
   assert [ -f ${SNAP} ]
   assert [ ! -f ${CORE} ]
   check_kmcore ${SNAP}
   run km_with_timeout ${SNAP} --some args
   assert_failure
   assert_output --partial "cannot set payload arguments when resuming a snapshot"
   assert [ ! -f ${CORE} ]
   rm -f ${SNAP} ${KMLOG} ${SNAP_OUTPUT} ${SNAP_INPUT}
}

@test "futex_snapshot($test_type): futex_snapshot and resume (futex_test$ext)" {
   SNAP=/tmp/snap.$$
   CORE=/tmp/core.$$

   for i in $(seq 100) ; do
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} futex_test$ext -s -l 100
      assert_success
      assert [ -f ${SNAP} ]
      assert [ ! -f ${CORE} ]
      check_kmcore ${SNAP}
      run km_with_timeout ${SNAP}
      assert_success
      refute_line --partial "state restoration error"
      assert [ ! -f ${CORE} ]
      rm -f ${SNAP}
   done
}

@test "km_main_shebang($test_type): shebang file handling (shebang$ext)" {
   KM_VERBOSE=generic run $KM_BIN ${KM_ARGS} shebang_test.sh AndEvenMore
   assert_success
   assert_line --partial "Extracting payload name from shebang file 'shebang_test.sh'"
   assert_line --partial "Found arg: 'arguments to test, should be one'"
   assert_line --partial "argv[3] = 'AndEvenMore'"

   # shebang to nested symlink
   KM_VERBOSE=generic run $KM_BIN ${KM_ARGS} shebang_test_link.sh AndEvenMore
   assert_success
   assert_line --partial "Extracting payload name from shebang file 'shebang_test_link.sh'"
   assert_line --partial "Found arg: 'arguments to test, should be one'"
   assert_line --partial "argv[3] = 'AndEvenMore'"
}

@test "km_main_symlink($test_type): symlink handling" {
   # single symlink
   run ./hello_test AndEvenMore
   assert_success
   assert_line --partial "argv[1] = 'AndEvenMore'"

   # double symlink
   run ./hello_test_link AndEvenMore
   assert_success
   assert_line --partial "argv[1] = 'AndEvenMore'"
}

@test "exec($test_type): test execve and execveat hypercalls (exec_test$ext)" {
   # Test execve()
   run km_with_timeout exec_test$ext
   assert_success
   assert_line --regexp "argv.0. = .*print_argenv_test"
   assert_line --partial "argv[4] = 'd4'"
   assert_line --partial "env[0] = 'ONE=one'"
   assert_line --partial "env[3] = 'FOUR=four'"

   # test execveat() which is fexecve()
   run km_with_timeout exec_test$ext -f
   assert_success
   assert_output --partial "Checking fexecve"
   assert_line --regexp "argv.0. = .*print_argenv_test"
   assert_line --partial "argv[4] = 'd4'"
   assert_line --partial "env[0] = 'ONE=one'"
   assert_line --partial "env[3] = 'FOUR=four'"

   # test correct stderr for ENOENT and ENOEXEC
   run km_with_timeout exec_test$ext -e
   assert_failure
   assert_output --partial "errno 2,"
   run km_with_timeout exec_test$ext -E
   assert_failure
   assert_output --partial "errno 8,"

   # test exec into shebang
   KM_EXEC_TEST_EXE=shebang_test.sh run km_with_timeout exec_test$ext
   assert_line --regexp 'argv\[0\] = .*tests/hello_test.km'
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"
   KM_EXEC_TEST_EXE=shebang_test.sh run km_with_timeout exec_test$ext -f
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"
   KM_EXEC_TEST_EXE=shebang_test_link.sh run km_with_timeout exec_test$ext
   assert_line --regexp 'argv\[0\] = .*tests/hello_test.km'
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"

   # test /bin/env in execve()
   run km_with_timeout exec_test$ext -S
   assert_success
   assert_line --regexp ".*Hello, argv\[0\] = .*hello_test.*"

   # test handling of env and sh paths in shabangs passed to km
   run km_with_timeout shebang_sh_test.sh
   assert_failure
   assert_line --partial "realpath(/bin/sh.km) failed: No such file or directory"

   run km_with_timeout shebang_env_test.sh
   assert_success
   assert_line --regexp ".*Hello, argv\[0\] = .*hello_test.*"


   # test that fork does not block SIGCHLD signal
   run km_with_timeout exec_target_test$ext parent_of_waitforchild
   assert_success
}

@test "exec_sh($test_type): test execve to /bin/sh and .km (exec_test$ext)" {
   # test exec in .km file
   run km_with_timeout exec_test$ext -k
   assert_success
   assert_line --partial "Hello, argv[1] = 'TESTING exec to .km'"

   # test exec into /bin/sh
   run km_with_timeout exec_test$ext -s
   assert_success
   assert_line --partial "Hello, argv[0] = './hello_test.km'"
   assert_line --partial "Hello, argv[3] = 'more quotes'"

   # test exec into realpath(/proc/self/exe)
   run km_with_timeout exec_test$ext -X
   assert_success
   assert_line --regexp "^/proc/self/exe resolved to .*/exec_test.km"
   assert_line --partial "noop: -0 requested"
}

@test "clock_gettime($test_type): VDSO clock_gettime, dependency on TSC (clock_gettime$ext)" {
   run km_with_timeout clock_gettime_test$ext -v
   assert_success
}

@test "utimensat_test($test_type): set mtime and atime with ns precision (utimensat_test$ext)" {
   run km_with_timeout utimensat_test$ext
   assert_success
}

@test "fork($test_type): fork, clone, exec, wait, kill test (fork_test$ext)" {
   run km_with_timeout fork_test$ext
   assert_success
}

@test "perf($test_type): measure time taken for dummy hypercall and page fault (perf_test$ext)" {
   run km_with_timeout perf_test$ext
   assert_success
}

@test "popen($test_type): popen pclose test (popen_test$ext)" {
   # use pipetarget_test to read /etc/group and pass through a popen pipe into a result file
   f1=/tmp/f1$$
   f2=/tmp/f2$$
   flog=/tmp/xx$$

   run km_with_timeout popen_test$ext /etc/group $f1 $f2
   assert_success
   diff /etc/group $f1
   assert_success
   diff /etc/group $f2
   assert_success

   # now test with a relative path to pipetarget_test but no place to find it, should fail
   run km_with_timeout --timeout 5s --putenv TESTPROG=pipetarget_test popen_test$ext /etc/group $f1 $f2
   assert_failure 1

   # now test with a relative path to pipetarget_test but with a PATH var
   run km_with_timeout --timeout 5s --putenv TESTPROG=pipetarget_test --putenv PATH="/usr/bin:." popen_test$ext /etc/group $f1 $f2
   assert_success

   # now test with full path to pipetarget_test and no PATH var.
   echo Error log is in $flog
   run km_with_timeout --timeout 5s -V --putenv TESTPROG=`pwd`/pipetarget_test -V popen_test$ext /etc/group $f1 $f2 2>$flog
   assert_success

   rm $f1 $f2
}

@test "semaphore($test_type): semaphore in shared memory test (semaphore_test$ext)" {
   # anon shared memory
   run gdb_with_timeout -ex="set pagination off" -ex="handle SIG63 nostop" -ex="set follow-fork-mode child" \
      -ex="source gdb_simple_test.py" -ex="run-test" -ex="q" --args ${KM_BIN} semaphore_test$ext -v
   assert_success
   assert_line --partial 'fail: 0'
   refute_line --partial "Couldn't turn off MAP_SHARED at kma"
   refute_line "Warning: Ignoring map counts. Please run this test in gdb to validate mmap counts"

   # file backed shared memory
   run gdb_with_timeout -ex="set pagination off" -ex="handle SIG63 nostop" -ex="set follow-fork-mode child" \
      -ex="source gdb_simple_test.py" -ex="run-test" -ex="q" --args ${KM_BIN} semaphore_test$ext file -v
   assert_success
   assert_line --partial 'fail: 0'
   refute_line --partial "Couldn't turn off MAP_SHARED at kma"
   refute_line "Warning: Ignoring map counts. Please run this test in gdb to validate mmap counts"
}

@test "files_on_exec($test_type): passing /proc and such to execed process (fs_exec_test$ext)" {
   run km_with_timeout --timeout 5s fs_exec_test$ext parent
   assert_success
   assert_line --regexp "parent exe: /[^[:space:]]*/tests/fs_exec_test$ext parent"
   assert_line --regexp "child  exe: /[^[:space:]]*/tests/fs_exec_test.km child"
}

@test "readlink_argv($test_type): readlink(argv[0]) should return .km file (readlink_argv0_test$ext)" {
   run ./readlink_argv0_test
   assert_success
   assert_line --regexp "slink=/[^[:space:]]*/tests/readlink_argv0_test$ext"
}

#
# Create 100 threads that run for 3 seconds and then terminate.
# This tries to expose problems that may occur when there are many threads in a payload.
# We should push this up to KVM_MAX_VCPU but that exposes a bug with how we dup fd's for km
# internal files up to the top of the fd space.
# In addition this may expose other problems such as misdeclared km arrays like km_hcargs[].
#
@test "threads_create($test_type): create a large number of threads that run briefly (gdb_lots_of_threads$ext)" {
   run km_with_timeout --timeout 5s gdb_lots_of_threads_test$ext -a 2 -t 287
   assert_success
}

@test "xstate_test($test_type): verify cpu extended state during context switch and signal handing (xstate_test$ext)" {
   run km_with_timeout xstate_test$ext
   assert_success
}

@test "km_logging($test_type): test the --km-log-to flag (hello_test$ext)" {
   LOGFILE="/tmp/km_$$.log"
   # We need KM_ARGS but we need to control the logging settings for this test
   KM_ARGS_PRIVATE=`echo $KM_ARGS | sed -e "s/--km-log-to=stderr//"`

   # Verify logging goes to stderr
   rm -f $LOGFILE
   assert ${KM_BIN} -V ${KM_ARGS_PRIVATE} ${KM_LDSO_ARGS} hello_test$ext 2>$LOGFILE
   assert test -e $LOGFILE
   assert grep -q "calling hc = 231 (exit_group)" $LOGFILE
   rm -f LOGFILE

   # Verify that logging when stderr is a pipe will switch logging to /tmp/km_XXXXX.log
   ${KM_BIN} -V ${KM_ARGS_PRIVATE} ${KM_LDSO_ARGS} hello_test$ext 2>&1 | grep -v matchnothing >$LOGFILE
   assert_success
   run grep -q "calling hc = 231 (exit_group)" $LOGFILE
   assert_failure
   run grep -q "Switch km logging to" $LOGFILE
   assert_success
   KMLOGFILE=`grep "Switch km logging to" $LOGFILE | sed -e "s/Switch km logging to //" | sed -e "s/ on first attempt to log//"`
   assert test -e $KMLOGFILE
   assert grep -q "calling hc = 231 (exit_group)" $KMLOGFILE
   rm -f $KMLOGFILE
   rm -f LOGFILE

   # Verify that we can force logging to stderr even if it is a pipe
   ${KM_BIN} -V --km-log-to=stderr ${KM_ARGS_PRIVATE} ${KM_LDSO_ARGS} hello_test$ext 2>&1 | grep -v matchnothing >$LOGFILE
   assert_success
   test -e $LOGFILE
   assert_success
   run grep -q "Switch km logging to" $LOGFILE
   assert_failure
   run grep -q "calling hc = 231 (exit_group)" $LOGFILE
   assert_success
   rm -f $LOGFILE

   # Verify that we can force logging to be disabled
   # Note that some logging does happen before it can be disabled
   ${KM_BIN} -V --km-log-to=none ${KM_ARGS_PRIVATE} ${KM_LDSO_ARGS} hello_test$ext &>$LOGFILE
   assert_success
   test -e $LOGFILE
   assert_success
   run grep -q "calling hc = 231 (exit_group)" $LOGFILE
   assert_failure
   rm -f $LOGFILE
}

# Verify that gdb can follow an execve() system call to the new executable.
# Verify that km can cause the payload to pause in the child after a fork to allow gdb client to attach
# to the child
@test "gdb_forkexec($test_type): test post fork gdb attach and gdb follow exec (gdb_forker_test$ext)" {
   # get the gdb ports km will use.  This test currently uses 4 ports since it forks 3 copies of itself.
   # We don't connect to the 2 youngest child processes and those children are not important for the test.
   # So, the next free port would be 19
   local port_id=15
   local km_gdb_port=$(( $port_range_start + $port_id))

   # start the test program up under gdb control. KM_GDB_CHILD_FORK_WAIT must be in km's env, not the payload's
   KM_GDB_CHILD_FORK_WAIT=".*gdb_forker_test.*" km_with_timeout -g$km_gdb_port gdb_forker_test$ext &
   local pid=$!

   # attach gdb client to the test's parent process
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port" --ex=c --ex=q &

   # attach a second gdb client to the test child process.  This verifies that we attach after the
   # fork in the child and we follow the exec to the next program.
   local km_gdb_port2=$(( $km_gdb_port + 1))
   run gdb_with_timeout -q -nx --ex="target remote :$km_gdb_port2" --ex="source cmd_for_forkexec_test.gdb" --ex=c --ex=q
   assert_success

   wait_and_check $pid 0

   assert_line --regexp "post fork prog: .*gdb_forker_test.*"
   # verify that gdb was notified of the exec to a new program
   assert_line --regexp "Remote target is executing new program:.*hello_test.*"
   assert_line --regexp "Catchpoint 1 .*hello_test.*_start.*"
   # verify that the  post exec prog name is as expected
   assert_line --regexp "post exec prog: .*hello_test.*"
}

# This test can fail when multiple instances of make tests in running simultaneously
# deletion and creation of /dev/kontain has race condition running in parallel
# run only one version.
@test "km_identity($test_type): kontain device node test (hello_test$ext)" {
   local KM_VDEV_NAME="/tmp/kontain$$"
   rm -f ${KM_VDEV_NAME}
   ln -s /dev/${USE_VIRT} ${KM_VDEV_NAME}
   run ${KM_BIN} --km-log-to=stderr -Vkvm --virt-device=${KM_VDEV_NAME} hello_test$ext
   assert_success
   assert_output --partial "Using device file ${KM_VDEV_NAME}"
   if [[ "${USE_VIRT}" == kvm ]]; then
      assert_output --partial "Setting vm type to VM_TYPE_KVM"
   else
      assert_output --partial "Setting vm type to VM_TYPE_KKM"
   fi
   rm -f ${KM_VDEV_NAME}
}

@test "km_sid_pgid($test_type): test session id and process group id hypercalls (sid_pgid_test$ext)" {
   run km_with_timeout sid_pgid_test$ext
   assert_success
}

@test "km_exec_guest_files($test_type): verify that open files are recovered across execve() (exec_guest_files_test$ext)" {
   # This test used 2 network ports.  The next free port would be 21.
   local port_id=19
   local socket_port=$(($port_range_start + $port_id))
   KMTRACE=/tmp/exec_guest_files_kmtrace.$$

   rm -f $KMTRACE
   SOCKET_PORT=$socket_port KM_VERBOSE="" run km_with_timeout --km-log-to=$KMTRACE exec_guest_files_test$ext exec_to_target
   assert_success
   diff <(grep "before exec" $KMTRACE | cut -b 57-)  <(grep "after exec" $KMTRACE | cut -b 56-)
   assert_success
   rm -f $KMTRACE
}
