#
# Basic tests setup for Kontain Monitor (KM).
# Used from .bats files (i.e. 'load tests_setup')
#
# See ./bats  for BATS details.

# TODO; check it KM is built already

# we will kill any test if takes longer
timeout=60s

# this is how we invoke KM. Note that upstairs will add <test>.km to this
KM="timeout -v --foreground $timeout ../build/km/km"

# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

# helper function to print content of $output to bats stderr, which in bats contains
# stdout + stderr
# if passed $1, will print it before $output
emit_debug_output() {

  printf '%s%s%s\n' 'output:' "$1" "$output" >&2
}
