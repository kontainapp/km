#!/bin/bash
# Nokia environment create modified for Kontain build environment.

#set -o errexit
set -e ; [ "${TRACE}" ] && set -x

KM_BASE=$(git rev-parse --show-toplevel)

BASE_DIR=${KM_BASE}/build/demo/nokia/disks
# set CPREFIX environment (before running test) to run Kontain inages.E.g. 'export CPREFIX=kontain/nokia'
CPREFIX=${CPREFIX:-kontainstage.azurecr.io/nokia/ckaf}
CPREFIX_CLIENT=${CPREFIX_CLIENT:-kontainstage.azurecr.io/nokia/atg}
SLEEP=${SLEEP:-10}
# use RM_FLAG="--label whatever" to cancel container removal at the end of the run
RM_FLAG=${RM_FLAG:---rm}
# original was 122880:122880, not sure why
NOFILE=${NOFILE:-122880:122880}

if [ -z $1 ]; then
    echo "usage: createTestEnv.sh <number_of_instance>"
    exit 1
fi

NUMBER_OF_INSTANCE=$1

echo "Cleaning disks"
sudo rm -Rf $BASE_DIR
mkdir -p $BASE_DIR

echo "Run zookeeper"
mkdir -p $BASE_DIR/zookeeper/log && \
mkdir -p $BASE_DIR/zookeeper/data && \
docker run --device=/dev/kvm --name zookeeper-server -p 2181:2181 --network kafka-net -e ZOOKEEPER_CLIENT_PORT=2181 \
    -e ZOOKEEPER_TICK_TIME=2000 \
    -e ZOOKEEPER_SYNC_LIMIT=2 \
    -e IS_RESTORE=false \
    -v $BASE_DIR/zookeeper/log:/var/lib/zookeeper/log:z \
    -v $BASE_DIR/zookeeper/data:/var/lib/zookeeper/data:z \
    --ulimit nofile=${NOFILE} \
   ${RM_FLAG} -d ${CPREFIX}/zookeeper:2.0.0-3.4.14-2696
   sleep ${SLEEP}


for (( i=1; i<=$NUMBER_OF_INSTANCE; i++ ))
do
   echo "Run kafka-broker-$i"
    mkdir -p $BASE_DIR/kafka-broker-$i/data && \
    mkdir -p $BASE_DIR/kafka-broker-$i/log && \
    docker run --device=/dev/kvm --name kafka-broker-$i -p 1909$(( $i + 1 )):9092 --network kafka-net \
        -e KAFKA_ZOOKEEPER_CONNECT=zookeeper-server:2181 \
        -e IS_RESTORE=false \
        -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://kafka-broker-$i:9092 \
        -e INTERNAL_SECURITY_PROTOCOL=PLAINTEXT \
        -e EXTERNAL_SECURITY_PROTOCOL=PLAINTEXT \
        -v $BASE_DIR/kafka-broker-$i/data:/var/lib/kafka/data:z \
        -v $BASE_DIR/kafka-broker-$i/log:/var/log/kafka:z \
        --ulimit nofile=${NOFILE} \
        ${RM_FLAG} -d ${CPREFIX}/kafka:2.0.0-5.3.1-2696
        sleep ${SLEEP}
done


echo "Run test client"
docker run --name kafka-test-client \
    --network kafka-net \
    --ulimit nofile=${NOFILE} \
    --rm -d \
    ${CPREFIX_CLIENT}/kafka-client:4.1.2-2 \
    tail -f /dev/null



