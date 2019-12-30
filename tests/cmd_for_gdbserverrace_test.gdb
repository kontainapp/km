# Simple test for gdb stub
#
# in the tests directory:
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -Vgdb -g 2159 ./gdb_server_entry_race_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_gdbserverrace_test.gdb" --ex=c --ex=q gdb_server_entry_race_test.km
#
set logging on
set trace-commands on

handle SIGILL nostop

br hit_breakpoint
commands
  bt
  continue
end

set pagination off

define hook-quit
  set confirm off
end

cont
