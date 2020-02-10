# Test to verify that symbols brought in by dlopen() are available.
#
#  shell_1> ../build/km/km -g ./gdb_sharedlib2_test.kmd
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="source cmd_for_sharedlib2_test.gdb"  --ex c --ex q
#

set pagination off

# at this point libcrypt.so is loaded so place a breakpoint using a symbol
# from the loaded library
br got_symbol_breakpoint
commands
  br xcrypt
  continue
end

br hit_breakpoint
commands
  info sharedlibrary
  p symvalue
  disassem symvalue
  continue
end

continue

# we've hit the breakpoint placed on xcrypt
printf "Hit the breakpoint at xcrypt\n"
backtrace
print $rip
continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
