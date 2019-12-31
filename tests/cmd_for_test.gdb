# Simple test for gdb stub
#
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -g 2159 ./gdb_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_test.gdb"  --ex c --ex q gdb_test.km
#
# to validate the same with gdb server, use the same commmand for gdb,
# but replace 'gdb_test.km' with 'gdb_test', and use gdbserver"
#  shell_2> gdbserver :2159 gdb_test


set print pretty
set pagination off

break change_and_print
command
  bt
  p var1
end

hbreak rand_func if i > 1
command
  p/s "** HWBREAK "
  p/x i
end


p/s "** should break at change_and_print():"
continue

p/s "** should break at rand_func():"
continue

set $actual = var1
set $expected = 454201677

del
watch var1
command
bt
p/s "*** note: for now WATCH is reported as SIGTRAP in km gdbstub"
p var1
end

# cont to watchpoint
continue
if var1 != 0
p/s "VAR1 failed to set to 0, setting result to FAILURE"
set $actual = 0
end

# we are done, report the result
if $actual == $expected
p/s "SUCCESS"
else
p/s "FAILURE"
# when we have malloc():
# call printf("actual=%d, expected=%d\n", $actual, $expected)
# for now:
p "** actual: "
p $actual
p "** expected:"
print $expected
end

p/s "** should finish now"
del
i b

# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
