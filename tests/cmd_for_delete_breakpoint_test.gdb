# Simple test for deleting pending breakpoints
#
# in the tests directory:
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -Vgdb -g 2159 ./gdb_delete_breakpoint_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_delete_breakpoint_test.gdb" --ex=c \
#             --ex=q gdb_delete_breakpoint_test.km
#

br disable_breakpoint
commands
  disable breakpoint 3
  continue
end

br enable_breakpoint
commands
  enable breakpoint 3
  continue
end

# This is breakpoint 3
br hit_breakpoint
commands
  print "Hit breakpoint"
  continue
end

set pagination off

define hook-quit
  set confirm off
end

cont
