#!hello_test_link arguments to test, should be one
#
# Only the first line is looked at when passed to KM as a payload file
# The rest are ignored for now, but in the future we may add here config info
# or some build-in fun,  e.g.
snapshot_interval 15sec
snapshot_dir /mySnapshots
support_shell false
verbose (mmap|mem)
gdb_listen true

