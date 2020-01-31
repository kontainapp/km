# Test to verify that symbols brought in by dlopen() are available.
#
#  shell_1> ../build/km/km -g ./dlopen_exp.kmd
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="source cmd_for_dleopn_exp_test.gdb"  --ex c --ex q
#

set pagination off

br hit_breakpoint
commands
  info sharedlibrary
  p symvalue
  disassem symvalue
  continue
end

#
continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
