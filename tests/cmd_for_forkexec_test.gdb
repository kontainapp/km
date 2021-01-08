# We should be attaching after the fork returns in the child process.
# This happens because weve set the KM_GDB_FORK_CHILD_WAIT environment variable to have
# km's gdb stub pause after fork in the child.
# Print the program path so we can verify it is what we expect.
backtrace
frame 2
printf "post fork prog: %s\n", argv[0]
catch exec
cont
# At this point we should be at _start of what the child process exec'ed to.
# Run up to main() and then print out the path of what is currently running
# so can verify the exec happened.
br main
cont
backtrace
printf "post exec prog: %s\n", argv[0]
cont
quit
