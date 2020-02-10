#!/bin/bash
# Nokia environment create modified for Kontain build environment.

#set -o errexit

KM_BASE=$(git rev-parse --show-toplevel)

IMAGE_DIR=${KM_BASE}/build/demo/nokia/km_kafka_image
BASE_DIR=${KM_BASE}/build/demo/nokia/disks
CPREFIX=kontainstage.azurecr.io/nokia

if [ -z $1 ]; then
    echo "usage: destroyTestEnv.sh <number_of_instance>"
    exit 1
fi

NUMBER_OF_INSTANCE=$1

echo "Cleaning disks"
rm -Rf $BASE_DIR
mkdir -p $BASE_DIR

echo "Run zookeeper"
mkdir -p $BASE_DIR/zookeeper/log && \
mkdir -p $BASE_DIR/zookeeper/data && \
cat <<-EOF > ${BASE_DIR}/zookeeper/zookeeper.cfg
dataDir=${BASE_DIR}/zookeeper/data
# the port at which the clients will connect
clientPort=2181
# disable the per-ip limit on the number of connections since this is a non-production config
maxClientCnxns=0
autopurge.snapRetainCount=3
autopurge.purgeInterval=1
EOF
#(cd ${KM_BASE}/payloads/java/jdk-11+28; \
#strace -f ${KM_BASE}/payloads/java/jdk-11+28/build/linux-x86_64-server-release/images/jdk/bin/java \
#  -Xmx512M -Xms512M -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 \
#  -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true \
#  -Xlog:gc*:file=${BASE_DIR}/zookeeper/log/zookeeper-gc.log:time,tags:filecount=10,filesize=102400 \
#  -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false \
#  -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=${BASE_DIR}/zookeeper/log \
#  -Dlog4j.configuration=file:${IMAGE_DIR}/etc_kafka/log4j.properties \
#  -cp "${IMAGE_DIR}/usr_share_java_kafka/"'*' \
#  org.apache.zookeeper.server.quorum.QuorumPeerMain \
#  ${IMAGE_DIR}/etc_kafka/km_zookeeper.properties > ${BASE_DIR}/zookeeper/out.txt 2>&1
#) &
#sleep 5
#exit 0

# Use -Djava.compiler=NONE to disable JIT
# JIT: -XX:+PrintCompilation -XX:+PrintCodeCacheOnCompilation \
(cd ${KM_BASE}/payloads/java/jdk-11+28; \
${KM_BASE}/build/km/km --dynlinker=${KM_BASE}/build/runtime/libc.so \
  --putenv="LD_LIBRARY_PATH=${KM_BASE}/payloads/java/jdk-11+28/build/linux-x86_64-server-release/images/jdk/lib/server:${KM_BASE}/payloads/java/jdk-11+28/build/linux-x86_64-server-release/images/jdk/lib:/opt/kontain/lib64:/lib64" \
  ${KM_BASE}/payloads/java/jdk-11+28/build/linux-x86_64-server-release/images/jdk/bin/java.kmd \
  -Xmx512M -Xms512M -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 \
  -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true \
  -Xlog:gc*:file=${BASE_DIR}/zookeeper/log/zookeeper-gc.log:time,tags:filecount=10,filesize=102400 \
  -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false \
  -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=${BASE_DIR}/zookeeper/log \
  -Dlog4j.configuration=file:${IMAGE_DIR}/etc_kafka/log4j.properties \
  -cp "${IMAGE_DIR}/usr_share_java_kafka/"'*' \
  org.apache.zookeeper.server.quorum.QuorumPeerMain \
  ${IMAGE_DIR}/etc_kafka/km_zookeeper.properties > ${BASE_DIR}/zookeeper/out.txt 2>&1
) &
sleep 5
exit 0

for (( i=1; i<=$NUMBER_OF_INSTANCE; i++ ))
do
# log.dirs=/var/lib/kafka/data
# listeners=PLAINTEXT://0.0.0.0:9092
# zookeeper.connect=zookeeper-server:2181
# advertised.listeners=PLAINTEXT://kafka-broker-1:9092
    mkdir -p $BASE_DIR/kafka-broker-$i/data && \
    mkdir -p $BASE_DIR/kafka-broker-$i/log
    cat <<-EOF > ${BASE_DIR}/kafka-broker-$i/kafka.properties
log.dirs=${BASE_DIR}/kafka-broker-$i/log
listeners=PLAINTEXT://127.0.1.$i:9092
zookeeper.connect=localhost:2181
zookeeper.sasl.client=false
advertised.listeners=PLAINTEXT://127.0.1.1:9092
EOF
    (cd ${KM_BASE}/payloads/java/jdk-11+28; \
    ${KM_BASE}/build/km/km --dynlinker=${KM_BASE}/build/runtime/libc.so \
      --putenv="LD_LIBRARY_PATH=/opt/kontain/lib64:/lib64:./build/linux-x86_64-server-release/jdk/lib/" \
      ./build/linux-x86_64-server-release/jdk/bin/java.kmd \
       -Xmx1G -Xms1G -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:InitiatingHeapOccupancyPercent=35 \
       -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true \
       -Xlog:gc*:file=${BASE_DIR}/kafka-broker-$i/log/kafkaServer-gc.log:time,tags:filecount=10,filesize=102400 \
       -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false \
       -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=${BASE_DIR}/kafka-broker-$i/log/kafka \
       -Dlog4j.configuration=file:${IMAGE_DIR}/etc_kafka/log4j.properties \
       -Djava.compiler=NONE \
       -cp "${IMAGE_DIR}/usr_share_java_kafka/"'*' \
       kafka.Kafka ${BASE_DIR}/kafka-broker-$i/kafka.properties > ${BASE_DIR}/kafka-broker-$i/out.txt 2>&1
    ) &
    sleep 5
done
sleep 20
exit 0
docker run --name zookeeper-server -p 2181:2181 --network kafka-net -e ZOOKEEPER_CLIENT_PORT=2181 \
    -e ZOOKEEPER_TICK_TIME=2000 \
    -e ZOOKEEPER_SYNC_LIMIT=2 \
    -e IS_RESTORE=false \
    -v $BASE_DIR/zookeeper/log:/var/lib/zookeeper/log:z \
    -v $BASE_DIR/zookeeper/data:/var/lib/zookeeper/data:z \
    --ulimit nofile=122880:122880 \
    --rm -d ${CPREFIX}/ckaf/zookeeper:2.0.0-3.4.14-2696


for (( i=1; i<=$NUMBER_OF_INSTANCE; i++ ))
do
   echo "Run kafka-broker-$i"
    mkdir -p $BASE_DIR/kafka-broker-$i/data && \
    mkdir -p $BASE_DIR/kafka-broker-$i/log && \
    docker run --name kafka-broker-$i -p 1909$(( $i + 1 )):9092 --network kafka-net \
        -e KAFKA_ZOOKEEPER_CONNECT=zookeeper-server:2181 \
        -e IS_RESTORE=false \
        -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://kafka-broker-$i:9092 \
        -e INTERNAL_SECURITY_PROTOCOL=PLAINTEXT \
        -e EXTERNAL_SECURITY_PROTOCOL=PLAINTEXT \
        -v $BASE_DIR/kafka-broker-$i/data:/var/lib/kafka/data:z \
        -v $BASE_DIR/kafka-broker-$i/log:/var/log/kafka:z \
        --ulimit nofile=122880:122880 \
        -d ${CPREFIX}/ckaf/kafka:2.0.0-5.3.1-2696
done


echo "Run test client"
docker run --name kafka-test-client \
    --network kafka-net \
    --ulimit nofile=122880:122880 \
    --rm -d \
    ${CPREFIX}/atg/kafka-client:4.1.2-2 \
    tail -f /dev/null



