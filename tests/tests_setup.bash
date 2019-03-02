#
# Basic tests setup for Kontain Monitor (KM).
# Used from .bats files (i.e. 'load tests_setup')
#
# See ./bats-core  for BATS details.

#TODO; check it KM is built already
KM=../build/km/km
TERM=xterm

emit_debug_output() {
  printf '%s\n' 'output:' "$output" >&2
}
