
REPLICATION_FACTOR=3
PARTITIONS=6
NUMBER_OF_RECORDS=50000000
#NUMBER_OF_RECORDS=5000
ACKS=1




./createTestEnv.sh $REPLICATION_FACTOR \
&& \
echo "Sleeping ..." \
&& \
sleep 15 \
&& \
docker exec -it kafka-test-client /usr/bin/kafka-topics \
    --zookeeper zookeeper-server:2181 \
    --create --topic test \
    --partitions $PARTITIONS \
    --replication-factor $REPLICATION_FACTOR \
&& \
echo "Starting test ..." \
&& \
docker exec -it kafka-test-client /usr/bin/kafka-run-class \
    org.apache.kafka.tools.ProducerPerformance \
    --topic test \
    --num-records $NUMBER_OF_RECORDS \
    --record-size 100  \
    --throughput -1 \
    --print-metrics \
    --producer-props acks=$ACKS \
    bootstrap.servers=kafka-broker-1:9092 \
    buffer.memory=67108864 batch.size=8196 \
    request.timeout.ms=60000 \
&& \
echo "Stopping test ..." \
&& \
./destroyTestEnv.sh $REPLICATION_FACTOR