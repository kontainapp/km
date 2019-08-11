# prints mmaps status on dummy_hcall.
# mprotect_test.c is expected to call dummy_Hcall after each test
# so we can automate maps validation

source ../km/gdb/list.gdb

break km_hcalls.c:dummy_hcall
command
print "Busy list"
print_tailq &machine.mmaps.busy
print "Free list"
print_tailq &machine.mmaps.free
end
