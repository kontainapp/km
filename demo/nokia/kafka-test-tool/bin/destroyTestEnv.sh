#!/bin/bash
NUMBER_OF_INSTANCE=$1

if [ -z $1 ]; then
    echo "usage: destroyTestEnv.sh <number_of_instance>"
    exit 1
fi

docker stop zookeeper-server  kafka-test-client

for (( i=1; i<=$NUMBER_OF_INSTANCE; i++ ))
do
    docker stop kafka-broker-$i
done
