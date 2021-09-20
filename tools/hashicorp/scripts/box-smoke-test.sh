#!/bin/bash
#
# A simple test to see if we can do simple things in an aws ami or vagrant box
# Are km, krun, docker, and podman present.
# Are podman and docker configurations setup properly to use krun and km.
#
# Supposedly vagrant will pass environment variables through ssh if they are
# listed in the Vagrant file with 'config.ssh.forward_env = [ "TRACE" ]'
# It didn't work for me.
set -x
#set -e
lscpu
ls -laRZ /opt/kontain
ls -lZ /dev/k*m
/opt/kontain/bin/km /opt/kontain/tests/hello_test.km 1 2 3 4 5 6
RESULT="abc 123 xyz 789"
CONTAINER=kontainapp/runenv-dweb
docker run --rm --init --runtime krun $CONTAINER /bin/sh -c "echo $RESULT" | grep "$RESULT"
sudo journalctl -k
podman run --rm --init --runtime krun docker.io/$CONTAINER /bin/sh -c "echo $RESULT" | grep "$RESULT"
sudo journalctl -k
