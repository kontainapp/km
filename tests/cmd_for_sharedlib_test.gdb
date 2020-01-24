# Test to see if the "info auxv" and "info sharedlibrary" commands are
# producing output that is sensible.
# The "info sharedlibrary" test only makes sense for the .kmd version
# of the test program
#
# to test gdb attaching at entry to the dynamic linker
#  shell_1> ../build/km/km -G ./stray_test.kmd stray
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="source cmd_for_sharedlib_test.gdb"  --ex c --ex q
# We also us -g with this test script to test for entry at _start.
#

set pagination off

info auxv

info sharedlibrary

# run til segfault
continue

#
continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
