#!/bin/bash
#
# Copy km and python.km artifacts into provided container 

if [[ $# -ne 0 ]] ; then CONTAINER=$1 ; else echo "Copy where?" && exit 1 ; fi

docker cp ~/workspace/km/payloads/python/cpython/python.kmd ${CONTAINER}:/usr/bin/python3.8.km
docker cp /opt/kontain/ ${CONTAINER}:/opt
docker cp ~/workspace/km/build/km/km ${CONTAINER}:/opt/kontain/bin/km
docker cp ~/workspace/km/build/runtime/libc.so ${CONTAINER}:/opt/kontain/runtime/libc.so
docker exec ${CONTAINER} ln -sf /opt/kontain/bin/km /usr/bin/python3.8
docker exec ${CONTAINER} ln -sf /opt/kontain/runtime/libc.so /opt/kontain/runtime/ld-linux-x86-64.so.2