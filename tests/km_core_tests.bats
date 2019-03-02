#
# BATS test set
#
# TODO:
#  - Add header and comments

load tests_setup

@test "basic vm setup, workload invocation and exit value check" {
   run $KM exit_value_test.km
   if [[ $status -ne 0  || $(echo "$output" | grep -cw 'status 0x11') != 1 ]]
   then
    emit_debug_output && return 1
   fi

}

@test "load elf and layout check" {
   # Show this on failure:
   echo Failed - try to run \'make load_expected_size\' in tests, and replace load.c:size value

   # Now run the test
   run $KM load_test.km
   if [[ $status -ne 0  || $(echo "$output" | grep -cw 'status 0x0') != 1 ]]
   then
    emit_debug_output && return 1
   fi
}

@test "KVM memslot / phys mem sizes" {
   run ./memslot_test
   if [ $status -ne 0  ]
   then
    emit_debug_output && return 1
   fi
}

@test "brk() call" {
   run $KM brk_test.km
   if [ "$status" -ne 0 ]
   then
    emit_debug_output && return 1
   fi
}

@test "Hello world - run and print" {
   linux_out=`./hello_test`
   run $KM hello_test.km
   if [[ $status -ne 0  || $(echo "$output" | grep -cw "$linux_out") != 1 ]]
   then
    emit_debug_output && return 1
   fi
}

@test "Basic HTTP/socket I/O (hello_html)" {
   local expected="I'm here"
   local address="http://127.0.0.1:8002"

	(./hello_html_test &)
	run curl -s $address
   if [[ $status -ne 0  || $(echo "$output" | grep -cw "$expected") != 1 ]]
   then
    emit_debug_output && return 1
   fi

   ($KM hello_html_test.km &)
	# 'km' monitor may start slow (while we are experimenting with startup time)
	# let it time to start before running curl
	sleep 0.5s
	run curl -s $address
   if [[ $status -ne 0  || $(echo "$output" | grep -cw "$expected") != 1 ]]
   then
    emit_debug_output && return 1
   fi
}

@test "mmap/munmap" {
   run $KM mmap_test.km
   if [[ $status -ne 0  || $(echo "$output" | grep -cw 'status 0x0') != 1 ]]
   then
    emit_debug_output && return 1
   fi
}

@test "futex example" {
   skip "TODO: convert to test"

   run $KM futex.km
   [ "$status" -eq 0 ]
   echo "${output}" | grep -wq SUCCESS
}

@test "gdb support" {
   $KM -g 3333 gdb_test.km  &
	sleep 0.5
	run gdb -q -nx --ex="target remote :3333"  --ex="source cmd_for_test.gdb"  \
         --ex=c --ex=q gdb_test.km
   if [ $(echo "$output" | grep -cw 'SUCCESS') != 1 ]
   then
    emit_debug_output && return 1
   fi
}

@test "basic threads loop" {
   skip "TODO: convert to test"

   run $KM hello_t_loops_test.km
   [ "$status" -eq 0 ]
   echo "${output}" | grep -wq SUCCESS
}

@test "pthread_create and mutex" {
   skip "TODO: convert to test"

   run $KM mutex.km
   [ "$status" -eq 0 ]
   echo "${output}" | grep -wq SUCCESS
}