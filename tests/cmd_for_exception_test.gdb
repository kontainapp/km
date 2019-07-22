# Test for GDB stub handing of guest signals.
#
# to test km gdb signal support from ./tests:
#  shell_1> ../build/km/km -g3333 ./stray_test.km signal
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_signal_test.gdb"  --ex c --ex q stray_test.km
#

set print pretty
set pagination off

p/s "** should stop for SIGSEGV:"
continue

# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
