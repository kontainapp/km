#!/bin/bash
#
# A simple test to see if we can do simple things in an aws ami or vagrant box
# Are km, krun, docker, and podman present.
# Are podman and docker configurations setup properly to use krun and km.
#
[ "$TRACE" ] && set -x
set -e
/opt/kontain/bin/km /opt/kontain/tests/hello_test.km 1 2 3 4 5 6
RESULT="abc 123 xyz 789"
CONTAINER=kontainapp/runenv-dweb
docker run --rm --init --runtime krun $CONTAINER /bin/sh -c "echo $RESULT" | grep "$RESULT"
podman run --rm --init --runtime krun docker.io/$CONTAINER /bin/sh -c "echo $RESULT" | grep "$RESULT"

