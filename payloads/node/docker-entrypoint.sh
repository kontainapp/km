#!/bin/ash
# entrypoint exampe, borrowed from node, to test sh entrypoint
set -ex

if [ "${1#-}" != "${1}" ] || [ -z "$(command -v "${1}")" ]; then
  set -- node "$@"
fi

exec "$@"
