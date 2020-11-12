# Copyright © 2019-2020 Kontain Inc. All rights reserved.
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
# The bats tests need this behavour so, tell it to keep logging to stderr.
KM_ARGS="--km-log-to=stderr"

load test_helper

# Lists of tests to skip (space separated). Wildcards (glob) can be used, but please use '' for the whole list

# not_needed_{generic,static,dynamic,shared} - skip since it's not needed
# todo_{generic,static,dynamic,shared} - skip since it's a TODO
not_needed_generic=''
# TODO: gdb_delete_breakpoint and gdb_server_race are caused by race described in https://github.com/kontainapp/km/issues/821.
# Disable them for now to improve signal/noise ratio
todo_generic='gdb_delete_breakpoint gdb_server_race clock_gettime'

not_needed_static='gdb_sharedlib'
todo_static=''

# skip slow ones
not_needed_alpine_static='km_main_argv0 km_main_shebang km_main_symlink linux_exec setup_link setup_load gdb_sharedlib mem_regions threads_mutex sigaltstack mem_test readlink_argv'
# review - some fail. Some slow
todo_alpine_static='dl_iterate_phdr'

# glibc native
not_needed_glibc_static='setup_link setup_load gdb_sharedlib readlink_argv'

# exception - extra segment in kmcore
# dl_iterate_phdr - load starts at 4MB instead of 2MB
# filesys - dup3 flags check inconsistency between musl and glibc
# gdb_nextstep - uses clone_test, same as raw_clone
# raw_clone - glibc clone() wrapper needs pthread structure

todo_glibc_static='exception dl_iterate_phdr filesys gdb_nextstep raw_clone xstate_test '

not_needed_alpine_dynamic=$not_needed_alpine_static
todo_alpine_dynamic=$todo_alpine_static

# note: these are generally redundant as they are tested in 'static' pass
not_needed_dynamic='km_main_argv0 km_main_shebang km_main_symlink linux_exec setup_load mem_slots cli km_main_env mem_brk mmap_1 km_many readlink_argv'
todo_dynamic='mem_mmap exception cpp_ctors dl_iterate_phdr monitor_maps '

todo_so=''
not_needed_so='km_main_argv0 km_main_shebang km_main_symlink linux_exec setup_load cli mem_* file* gdb_* mmap_1 km_many hc_check \
    exception cpp_ctors dl_iterate_phdr monitor_maps pthread_cancel mutex vdso threads_mutex sigsuspend semaphore files_on_exec readlink_argv'

# make sure it does not leak in from the outer shell, it can mess out the output
unset KM_VERBOSE

# exclude more tests for Kontain Kernel Module (leading space *is* needed)
if [ "${USE_VIRT}" = 'kkm' ]; then
   not_needed_alpine_dynamic=$not_needed_alpine_static
   todo_generic+=' futex_snapshot'
fi

if [ "${USE_VIRT}" = 'kvm' ]; then
   todo_generic+=' xstate_test'
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
# Due to bats implementation peculiarities, it's hard to automate port number assignement so each test
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
   assert_failure 31  #SIGSYS
   assert_output --partial "Bad system call"

   run km_with_timeout stray_test$ext -- hc -10
   assert_failure 7   #SIGBUS
   assert_output --partial "Bus error"

   run km_with_timeout stray_test$ext hc 1000
   assert_failure 7   #SIGBUS
   assert_output --partial "Bus error"

   run km_with_timeout stray_test$ext hc-badarg 3
   assert_failure 11  #SIGSEGV
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
   KM_VERBOSE=generic run ./$payload SomeArg
   assert_success
   assert_line --partial "Setting payload name to ./$payload.km"
   assert_line --partial "argv[1] = 'SomeArg'"
}

@test "km_main_env($test_type): passing environment to payloads (env_test$ext)" {
   val=`pwd`/$$

   # --putenv defines an env var and cancels 'default' --copyenv
   run km_with_timeout --putenv PATH=$val env_test$ext
   assert_success
   assert_output --partial "PATH=$val"
   refute_output --partial "getenv: PATH=$PATH"

   # by default, --copyenv is enabled
   run km_with_timeout  env_test$ext
   assert_success
   assert_line "getenv: PATH=$PATH"

   # Putting --copyenv on command line is harmless
   run km_with_timeout --copyenv --copyenv env_test$ext
   assert_success
   assert_line "getenv: PATH=$PATH"

   # both --copyenv and --putenv on command line is not supported
   run km_with_timeout --copyenv --putenv MORE=less env_test$ext
   assert_failure
   run km_with_timeout --putenv PATH=testingpath --copyenv env_test$ext
   assert_failure

   run km_with_timeout --putenv PATH=$val env_test$ext
   assert_success
   assert_output --partial "PATH=$val"
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
   sleep 0.5s
   run curl -s $address
   assert_success
   linux_out="${output}"

   (km_with_timeout hello_html_test$ext $port &)
   sleep 0.5s
	run curl -s $address
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

   # make sure there is a filename somwewhere in the maps
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

# Block gdb port by running a python http server on it,
# then check that KM simply proceeds without gdb support
@test "km_many($test_type): running multiple KMs (hello_test$ext)" {
   (# force new shell to prevent $! races
   local port_id=1
   local km_gdb_port=$(( $port_range_start + $port_id))
   python3 -c "from http.server import * ; HTTPServer( ('', ${km_gdb_port}), BaseHTTPRequestHandler).serve_forever()" & \
         curl --silent --retry 5 --retry-connrefused 127.0.0.1:${km_gdb_port}
   local pid=$!
   run km_with_timeout -g${km_gdb_port} --gdb-listen hello_test$ext
   kill $pid
   assert_success
   assert_line --partial "disabling gdb support"
   )
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
   wait_and_check $pid 6 # expect KM to errx(6,...)
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
   wait_and_check $pid 11 # expect KM to exit with SIGSEGV
}

@test "gdb_server_race($test_type): gdb server concurrent wakeup test" {
   local port_id=5
   local km_gdb_port=$(( $port_range_start + $port_id))
   km_trace_file=/tmp/gdb_server_race_test_static_$$.out
   # Test with breakpoints triggering and SIGILL being happending continuously
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
      echo "# === Begin $km_trace_file for km, status=$status =====" >&3
      sed -e "s/^/# /" <$km_trace_file >&3
      echo "# === End   $km_trace_file =====" >&3
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
# the dynamic linker starts. -g attachs at the _start entry point.
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
   wait_and_check $pid 11 # expect KM to exit with SIGSEGV

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
   wait_and_check $pid 11 # expect KM to exit with SIGSEGV

   # There is no explicit test for vFile remote commands.  gdb uses vFile as part of
   # processing the "info sharedlibrary" command.

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

   # 2nd try to test asynch gdb client attach to the target
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
   run km_with_timeout hello_2_loops_test$ext
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
   assert_failure 8 # SIGFPE
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
   assert_failure 4 # SIGILL
   echo $output | grep -F 'Illegal instruction (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'undefined_op ('
   rm -f ${CORE}

   # page fault
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext stray
   assert_failure 11 # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'stray_reference ('
   rm -f ${CORE}

   # bad hcall
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext hc 400
   assert_failure 31 # SIGSYS
   echo $output | grep -F 'Bad system call (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'main ('
   rm -f ${CORE}

   # write to text (protected memory)
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext prot
   assert_failure 11  # SIGSEGV
   echo $output | grep -F 'Segmentation fault (core dumped)'
   [ -f ${CORE} ]
   check_kmcore ${CORE}
   gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'write_text ('
   assert rm -f ${CORE}

   # abort
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext abort
   assert_failure 6  # SIGABRT
   echo $output | grep -F 'Aborted (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'abort ('
   rm -f ${CORE}

   # quit
   assert [ ! -f ${CORE} ]
   run km_with_timeout --coredump=${CORE} stray_test$ext quit
   assert_failure 3  # SIGQUIT
   echo $output | grep -F 'Quit (core dumped)'
   assert [ -f ${CORE} ]
   check_kmcore ${CORE}
   [[ $test_type =~ (alpine|glibc)* ]] || gdb --ex=bt --ex=q stray_test$ext ${CORE} | grep -F 'kill ('
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
   run km_with_timeout -Vhyper signal_test$ext -v
   assert_success
   assert_line --partial "Ignoring tgid 100"
}

@test "pthread_cancel($test_type): (pthread_cancel_test$ext)" {
   run km_with_timeout pthread_cancel_test$ext -v
   assert_success
   assert_line --partial "thread_func(): end of DISABLE_CANCEL_TEST"
   refute_line --partial "PTHREAD_CANCEL_ASYNCHRONOUS"
   assert_line --partial "PTHREAD_CANCEL_DEFERRED"
}

# C++ tests
@test "cpp_ctors($test_type): constructors and statics (var_storage_test$ext)" {
   run km_with_timeout var_storage_test$ext
   assert_success

   ctors=`echo -e "$output" | grep -F Constructor | wc -l`
   dtors=`echo -e "$output" | grep -F Destructor | wc -l`
   assert [ "$ctors" -gt 0 ]
   assert [ "$ctors" -eq "$dtors" ]
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
   assert_line --regexp 'filesys_test.* \(1, #threads: 1\)'

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
   run km_with_timeout socket_test$ext
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
   assert_failure 6  # SIGABRT
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
   LINUXOUT=/tmp/sigsuspend_linuxout.$$
   KMOUT=/tmp/sigsuspend_kmout.$$

   rm -f $FLAGFILE $KMTRACE
   ./sigsuspend_test.fedora $FLAGFILE >$LINUXOUT &
   local pid=$!
   start=`date +%s`
   while [ ! -e $FLAGFILE ]
   do
      sleep .1
      now=`date +%s`
      test `expr $now - $start` -lt $WAIT
      assert_success
   done
   kill -SIGUSR1 $pid
   kill -SIGUSR1 $pid
   kill -SIGUSR2 $pid
   wait $pid
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
         echo "# === Begin $KMTRACE =====" >&3
         sed -e "s/^/# /" <$KMTRACE >&3
         echo "# === End   $KMTRACE =====" >&3
         fail "$FLAGFILE does not exist after $WAIT seconds!"
      fi
   done
   kill -SIGUSR1 $pid
   kill -SIGUSR1 $pid
   kill -SIGUSR2 $pid
   wait $pid
   rm -f $FLAGFILE

   # Debug.
   diff $LINUXOUT $KMOUT
   assert_success

   rm -f $KMTRACE $LINUXOUT $KMOUT
}

@test "itimer($test_type): test setitimer() getitimer() hypercalls (itimer_test$ext)" {
   run km_with_timeout itimer_test$ext
   assert_success
}

# In this series of test the "--" argument between --resume and ${SNAP} is important.
# It helps km_with_timeout() recognize that ${SNAP} is the payload name.
@test "basic_snapshot($test_type): snapshot and resume(snapshot_test$ext)" {
   SNAP=/tmp/snap.$$
   CORE=/tmp/core.$$
   KMLOG=/tmp/kmlog.$$

   for i in $(seq 100) ; do
      # snapshot resume that successfully exits
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext
      assert_success
      assert [ -f ${SNAP} ]
      assert [ ! -f ${CORE} ]
      check_kmcore ${SNAP}
      run km_with_timeout --km-log-to=${KMLOG} --resume -- ${SNAP}
      assert_success
      assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test applcation' ${KMLOG}
      assert [ ! -f ${CORE} ]
      rm -f ${SNAP} ${KMLOG}

      # snapshot with closed stdio
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext -c
      assert_success
      assert [ -f ${SNAP} ]
      assert [ ! -f ${CORE} ]
      check_kmcore ${SNAP}
      run km_with_timeout --km-log-to=${KMLOG} --resume -- ${SNAP}
      assert_success
      # TODO: remove the check with glibc_static when musl and glibc behave the same way
      [[ $test_type =~ glibc* ]] && refute_line --partial "Hello from thread"
      [[ $test_type =~ glibc* ]] || assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test applcation' ${KMLOG}
      assert [ ! -f ${CORE} ]
      rm -f ${SNAP} ${KMLOG}

      # snapshot resume that core dumps
      run km_with_timeout --coredump=${CORE} --snapshot=${SNAP} snapshot_test$ext -a
      assert_success
      assert [ -f ${SNAP} ]
      check_kmcore ${SNAP}
      run km_with_timeout --coredump=${CORE} --km-log-to=${KMLOG} --resume -- ${SNAP}
      assert_failure 6  # SIGABRT
      assert [ -f ${CORE} ]
      assert_output --partial "Hello from thread"
      refute_line --partial "state restoration error"
      assert grep 'label: snaptest_label' ${KMLOG}
      assert grep 'description: Snapshot test applcation' ${KMLOG}
      if [ "$test_type" = ".km.so" ]; then
         gdb --ex=bt --ex=q snapshot_test$ext ${CORE} | grep -F 'abort ('
      fi
      rm -f ${SNAP} ${CORE} ${KMLOG}
   done
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
      run km_with_timeout --resume -- ${SNAP}
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
   assert_line --partial "Adding extra arg 'arguments to test, should be one'"
   assert_line --partial "argv[3] = 'AndEvenMore'"

   # shebang to nested symlink
   KM_VERBOSE=generic run $KM_BIN ${KM_ARGS} shebang_test_link.sh AndEvenMore
   assert_success
   assert_line --partial "Extracting payload name from shebang file 'shebang_test_link.sh'"
   assert_line --partial "Adding extra arg 'arguments to test, should be one'"
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
   run km_with_timeout --copyenv exec_test$ext
   assert_success
   assert_line --regexp "argv.0. = .*print_argenv_test"
   assert_line --partial "argv[4] = 'd4'"
   assert_line --partial "env[0] = 'ONE=one'"
   assert_line --partial "env[3] = 'FOUR=four'"

   # test execveat() which is fexecve()
   run km_with_timeout --copyenv exec_test$ext -f
   assert_success
   assert_line --regexp "argv.0. = .*print_argenv_test"
   assert_line --partial "argv[4] = 'd4'"
   assert_line --partial "env[0] = 'ONE=one'"
   assert_line --partial "env[3] = 'FOUR=four'"

   # test exec into shebang
   KM_EXEC_TEST_EXE=shebang_test.sh run km_with_timeout exec_test$ext
   assert_line --regexp 'argv\[0\] = .*tests/hello_test.km'
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"
   KM_EXEC_TEST_EXE=shebang_test.sh run km_with_timeout --copyenv exec_test$ext -f
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"
   KM_EXEC_TEST_EXE=shebang_test_link.sh run km_with_timeout exec_test$ext
   assert_line --regexp 'argv\[0\] = .*tests/hello_test.km'
   assert_line --partial "argv[1] = 'arguments to test, should be one'"
   assert_line --partial "argv[6] = 'd4'"
}

@test "clock_gettime($test_type): VDSO clock_gettime, dependency on TSC (clock_gettime$ext)" {
   run km_with_timeout clock_gettime_test$ext -v
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

@test km_logging_test($test_type): test the --km-log-to flag (hello_test$ext)" {
   LOGFILE="km_$$.log"
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
