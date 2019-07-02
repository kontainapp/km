# Simple test for gdb stub
#
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -g 3333 ./gdb_test.km
#  shell_2> gdb -q -nx --ex="target remote :3333"  --ex="source cmd_for_test.gdb"  --ex c --ex q gdb_test.km
# 
# to validate the same with gdb server, use the same commmand for gdb,
# but replace 'gdb_test.km' with 'gdb_test', and use gdbserver"
#  shell_2> gdbserver :3333 gdb_test


set print pretty
set pagination off

p/s "** should stop for SIGUSR1:"
continue

p/s "** should stop for SIGABRT:"
continue

# For some reason 'cont' generates 'error in command file' 
# from gdb client for KM remote debug (but works with gdbserver) 
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
