# Test for GDB stub handing of qSupported and vCont packets
#
# to test km gdb signal support from ./tests:
#  shell_1> ../build/km/km -g ./gd_qsupported.km
#  shell_2> gdb -q -nx --ex="set debug remote 1" --ex="target remote :2159"  \
#                      --ex="source cmd_for_qsupported_test.gdb"  --ex c --ex q gdb_qsupported.km
#

set pagination off

br hit_breakpoint
command
  info threads
  continue
end

continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
