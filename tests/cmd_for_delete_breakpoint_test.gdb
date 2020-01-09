# Simple test for deleting pending breakpoints
#
# in the tests directory:
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -Vgdb -g 2159 ./gdb_delete_breakpoint_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_delete_breakpoint_test.gdb" --ex=c \
#             --ex=q gdb_delete_breakpoint_test.km
#

set $bp2ordinal = 4

br hit_breakpoint1
commands
  printf "Hit breakpoint1, bp2ordinal %d\n", $bp2ordinal
  del br $bp2ordinal
  continue
end

br hit_breakpoint2
commands
  printf "Hit breakpoint2, bp2ordinal %d\n", $bp2ordinal
  br hit_breakpoint_t2
  commands
    print "Hit breakpoint_t2"
    continue
  end
  set $bp2ordinal += 1
  continue
end

br hit_breakpoint_t1
commands
  print "Hit breakpoint_t1"
  continue
end

br hit_breakpoint_t2
commands
  print "Hit breakpoint_t2"
  continue
end

set pagination off

define hook-quit
  set confirm off
end

cont
