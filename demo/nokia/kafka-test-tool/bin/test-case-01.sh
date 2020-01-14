
REPLICATION_FACTOR=3
PARTITIONS=6
NUMBER_OF_RECORDS=50000000
ACKS=1
export COMPOSE_PROJECT_NAME=kontain

docker-compose up -d && \
echo "Sleeping ..." && \
sleep 15 && \
docker exec -it kontain_kafka-test-client_1 /usr/bin/kafka-topics \
    --zookeeper zookeeper-server:2181 \
    --create --topic test \
    --partitions $PARTITIONS \
    --replication-factor $REPLICATION_FACTOR &&\
docker exec -it kontain_kafka-test-client_1 /usr/bin/kafka-run-class \
    org.apache.kafka.tools.ProducerPerformance \
    --topic test \
    --num-records $NUMBER_OF_RECORDS \
    --record-size 100  \
    --throughput -1 \
    --print-metrics \
    --producer-props acks=$ACKS \
    bootstrap.servers=kafka-server2:9092 \
    buffer.memory=67108864 batch.size=8196 \
    request.timeout.ms=60000 &&\
docker-compose down