# Nokia Demo Directory

This directory contains demo work for Nokia Software. The directories
are:

* test-app - Python machine learning. (Suspended work for now)
* kafka-test-tool - Java based Kafka Tests

## kafka-test-tools

See `kafka-test-tools/README.md` for Nokia's instructions on this.

The needed images are in an Azure private container registry.
To retreive them into you local docker run:

```bash
make -C cloud/azure/ login
az acr login -n kontainstage
docker pull kontainstage.azurecr.io/nokia/ckaf/zookeeper:2.0.0-3.4.14-2696
docker pull kontainstage.azurecr.io/nokia/ckaf/kafka:2.0.0-5.3.1-2696
docker pull kontainstage.azurecr.io/nokia/atg/kafka-client:4.1.2-2
```

The `sysstat` package is also required:

```bash
sudo dnf install sysstat
```

### Tests

There are two separate tests included under `kafka-test-tools/bin`: `test-case-01.sh` and `test-case-ckaf-01.sh`.

`test-case-01.sh` is a  standard test that uses `docker-compose` and the
`bitnami` images from `dockerhub`.

```bash
cd demo/nokia/kafka-test-tool
./bin/startTest.sh test-case-01
```

`test-case-ckaf-01.sh` is Nokia's modifications and uses the docker images
from our private Azure container registry.

```bash
cd demo/nokia/kafka-test-tool/bin
./startTest.sh test-case-ckaf-01
```

These tests leave their results in `build/demo/nokia/results`. Each test run creates a sub-directory that contains a log file (`*-output.log`) and performance data (`*-sar.txt`).

Nokia uses `ksar` to visualize the performance data. Download a jar file from `https://github.com/vlsi/ksar/releases`.
To run:

```bash
java -jar ~/Downloads/ksar-5.2.4-b325_gdea8d8b5-SNAPSHOT-all.jar
```

## Private Notes

The following are John's private notes.

### Demo Notes (bitnami/docker-compose)

Zookeeper Cmd:

```bash
java -Dzookeeper.log.dir=/opt/bitnami/zookeeper/logs \
    -Dzookeeper.log.file=zookeeper--server-31d059bdf954.log \
    -Dzookeeper.root.logger=INFO,CONSOLE \
    -XX:+HeapDumpOnOutOfMemoryError \-XX:OnOutOfMemoryError="kill -9 %p" \
    -cp /opt/bitnami/zookeeper/bin/../zookeeper-server/target/classes:/opt/bitnami/zookeeper/bin/../build/classes:/opt/bitnami/zookeeper/bin/../zookeeper-server/target/lib/*.jar:/opt/bitnami/zookeeper/bin/../build/lib/*.jar:/opt/bitnami/zookeeper/bin/../lib/zookeeper-jute-3.5.6.jar:/opt/bitnami/zookeeper/bin/../lib/zookeeper-3.5.6.jar:/opt/bitnami/zookeeper/bin/../lib/slf4j-log4j12-1.7.25.jar:/opt/bitnami/zookeeper/bin/../lib/slf4j-api-1.7.25.jar:/opt/bitnami/zookeeper/bin/../lib/netty-transport-native-unix-common-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-transport-native-epoll-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-transport-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-resolver-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-handler-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-common-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-codec-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/netty-buffer-4.1.42.Final.jar:/opt/bitnami/zookeeper/bin/../lib/log4j-1.2.17.jar:/opt/bitnami/zookeeper/bin/../lib/json-simple-1.1.1.jar:/opt/bitnami/zookeeper/bin/../lib/jline-2.11.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-util-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-servlet-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-server-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-security-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-io-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/jetty-http-9.4.17.v20190418.jar:/opt/bitnami/zookeeper/bin/../lib/javax.servlet-api-3.1.0.jar:/opt/bitnami/zookeeper/bin/../lib/jackson-databind-2.9.10.jar:/opt/bitnami/zookeeper/bin/../lib/jackson-core-2.9.10.jar:/opt/bitnami/zookeeper/bin/../lib/jackson-annotations-2.9.10.jar:/opt/bitnami/zookeeper/bin/../lib/commons-cli-1.2.jar:/opt/bitnami/zookeeper/bin/../lib/audience-annotations-0.5.0.jar:/opt/bitnami/zookeeper/bin/../zookeeper-*.jar:/opt/bitnami/zookeeper/bin/../zookeeper-server/src/main/resources/lib/*.jar:/opt/bitnami/zookeeper/bin/../conf: \
    -Xmx1000m -Xmx1024m -Xms1024m -Dcom.sun.management.jmxremote -Dcom.sun.management.jmxremote.local.only=false org.apache.zookeeper.server.quorum.QuorumPeerMain /opt/bitnami/zookeeper/bin/../conf/zoo.cfg
```

Zookeeper Environment:

```bash
ZOO_DAEMON_GROUP=zookeeper
ZOO_CONF_DIR=/opt/bitnami/zookeeper/conf
ZOO_SERVERS=
ZOO_DAEMON_USER=zookeeper
ZOO_CLIENT_PASSWORD=
OS_FLAVOUR=debian-9
NAMI_PREFIX=/.nami
BITNAMI_PKG_CHMOD=-R g+rwX
HOSTNAME=31d059bdf954
ZOO_SERVER_USERS=
ZOO_HEAP_SIZE=1024
ZOO_AUTOPURGE_RETAIN_COUNT=3
ZOO_MAX_CLIENT_CNXNS=60
ZOO_ENABLE_AUTH=no
ZOO_LOG_DIR=/opt/bitnami/zookeeper/logs
ZOO_SERVER_ID=1
CLIENT_JVMFLAGS=-Xmx256m
ZOO_SERVER_PASSWORDS=
PWD=/
ZOO_4LW_COMMANDS_WHITELIST=srvr, mntr
HOME=/
ZOO_AUTOPURGE_INTERVAL=0
ZOO_SYNC_LIMIT=5
ZOO_CONF_FILE=/opt/bitnami/zookeeper/conf/zoo.cfg
OS_NAME=linux
ZOO_CLIENT_USER=
ZOO_RECONFIG_ENABLED=no
BITNAMI_DEBUG=false
ZOO_DATA_DIR=/bitnami/zookeeper/data
OS_ARCH=amd64
BITNAMI_APP_NAME=zookeeper
JVMFLAGS=-Xmx1000m   -Xmx1024m -Xms1024m
MODULE=zookeeper
ZOO_LOG_LEVEL=INFO
BITNAMI_IMAGE_VERSION=3.5.6-debian-9-r65
ZOO_BASE_DIR=/opt/bitnami/zookeeper
ALLOW_ANONYMOUS_LOGIN=yes
SHLVL=0
ZOO_BIN_DIR=/opt/bitnami/zookeeper/bin
ZOO_TICK_TIME=2000
SERVER_JVMFLAGS=-Xmx1000m
ZOO_PORT_NUMBER=2181
PATH=/opt/bitnami/java/bin:/opt/bitnami/zookeeper/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ZOO_INIT_LIMIT=10
```

Kafka Cmd:

```bash
java -Xmx1024m -Xms1024m -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true -Xloggc:/opt/bitnami/kafka/bin/../logs/kafkaServer-gc.log -verbose:gc -XX:+PrintGCDetails -XX:+PrintGCDateStamps -XX:+PrintGCTimeStamps -XX:+UseGCLogFileRotation -XX:NumberOfGCLogFiles=10 -XX:GCLogFileSize=100M -Dcom.sun.management.jmxremote -Dcom.sun.management.jmxremote.authenticate=false -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=/opt/bitnami/kafka/bin/../logs -Dlog4j.configuration=file:/opt/bitnami/kafka/bin/../config/log4j.properties -cp /opt/bitnami/kafka/bin/../libs/activation-1.1.1.jar:/opt/bitnami/kafka/bin/../libs/aopalliance-repackaged-2.5.0.jar:/opt/bitnami/kafka/bin/../libs/argparse4j-0.7.0.jar:/opt/bitnami/kafka/bin/../libs/audience-annotations-0.5.0.jar:/opt/bitnami/kafka/bin/../libs/commons-cli-1.4.jar:/opt/bitnami/kafka/bin/../libs/commons-lang3-3.8.1.jar:/opt/bitnami/kafka/bin/../libs/connect-api-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-basic-auth-extension-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-file-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-json-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-mirror-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-mirror-client-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-runtime-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/connect-transforms-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/guava-20.0.jar:/opt/bitnami/kafka/bin/../libs/hk2-api-2.5.0.jar:/opt/bitnami/kafka/bin/../libs/hk2-locator-2.5.0.jar:/opt/bitnami/kafka/bin/../libs/hk2-utils-2.5.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-annotations-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-core-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-databind-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-dataformat-csv-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-datatype-jdk8-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-jaxrs-base-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-jaxrs-json-provider-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-module-jaxb-annotations-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-module-paranamer-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jackson-module-scala_2.13-2.10.0.jar:/opt/bitnami/kafka/bin/../libs/jakarta.activation-api-1.2.1.jar:/opt/bitnami/kafka/bin/../libs/jakarta.annotation-api-1.3.4.jar:/opt/bitnami/kafka/bin/../libs/jakarta.inject-2.5.0.jar:/opt/bitnami/kafka/bin/../libs/jakarta.ws.rs-api-2.1.5.jar:/opt/bitnami/kafka/bin/../libs/jakarta.xml.bind-api-2.3.2.jar:/opt/bitnami/kafka/bin/../libs/javassist-3.22.0-CR2.jar:/opt/bitnami/kafka/bin/../libs/javax.servlet-api-3.1.0.jar:/opt/bitnami/kafka/bin/../libs/javax.ws.rs-api-2.1.1.jar:/opt/bitnami/kafka/bin/../libs/jaxb-api-2.3.0.jar:/opt/bitnami/kafka/bin/../libs/jersey-client-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-common-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-container-servlet-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-container-servlet-core-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-hk2-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-media-jaxb-2.28.jar:/opt/bitnami/kafka/bin/../libs/jersey-server-2.28.jar:/opt/bitnami/kafka/bin/../libs/jetty-client-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-continuation-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-http-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-io-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-security-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-server-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-servlet-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-servlets-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jetty-util-9.4.20.v20190813.jar:/opt/bitnami/kafka/bin/../libs/jopt-simple-5.0.4.jar:/opt/bitnami/kafka/bin/../libs/kafka-clients-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-log4j-appender-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-streams-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-streams-examples-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-streams-scala_2.13-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-streams-test-utils-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka-tools-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/kafka_2.13-2.4.0-sources.jar:/opt/bitnami/kafka/bin/../libs/kafka_2.13-2.4.0.jar:/opt/bitnami/kafka/bin/../libs/log4j-1.2.17.jar:/opt/bitnami/kafka/bin/../libs/lz4-java-1.6.0.jar:/opt/bitnami/kafka/bin/../libs/maven-artifact-3.6.1.jar:/opt/bitnami/kafka/bin/../libs/metrics-core-2.2.0.jar:/opt/bitnami/kafka/bin/../libs/netty-buffer-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-codec-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-common-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-handler-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-resolver-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-transport-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-transport-native-epoll-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/netty-transport-native-unix-common-4.1.42.Final.jar:/opt/bitnami/kafka/bin/../libs/osgi-resource-locator-1.0.1.jar:/opt/bitnami/kafka/bin/../libs/paranamer-2.8.jar:/opt/bitnami/kafka/bin/../libs/plexus-utils-3.2.0.jar:/opt/bitnami/kafka/bin/../libs/reflections-0.9.11.jar:/opt/bitnami/kafka/bin/../libs/rocksdbjni-5.18.3.jar:/opt/bitnami/kafka/bin/../libs/scala-collection-compat_2.13-2.1.2.jar:/opt/bitnami/kafka/bin/../libs/scala-java8-compat_2.13-0.9.0.jar:/opt/bitnami/kafka/bin/../libs/scala-library-2.13.1.jar:/opt/bitnami/kafka/bin/../libs/scala-logging_2.13-3.9.2.jar:/opt/bitnami/kafka/bin/../libs/scala-reflect-2.13.1.jar:/opt/bitnami/kafka/bin/../libs/slf4j-api-1.7.28.jar:/opt/bitnami/kafka/bin/../libs/slf4j-log4j12-1.7.28.jar:/opt/bitnami/kafka/bin/../libs/snappy-java-1.1.7.3.jar:/opt/bitnami/kafka/bin/../libs/validation-api-2.0.1.Final.jar:/opt/bitnami/kafka/bin/../libs/zookeeper-3.5.6.jar:/opt/bitnami/kafka/bin/../libs/zookeeper-jute-3.5.6.jar:/opt/bitnami/kafka/bin/../libs/zstd-jni-1.4.3-1.jar kafka.Kafka /opt/bitnami/kafka/conf/server.properties
```

Kafka Environment:

```bash
AFKA_CFG_LISTENERS=PLAINTEXT://:9092
KAFKA_CFG_ADVERTISED_LISTENERS=PLAINTEXT://kafka-server2:9092
KAFKA_HOME=/opt/bitnami/kafka
KAFKA_INTER_BROKER_PASSWORD=bitnami
OS_FLAVOUR=debian-9
NAMI_PREFIX=/.nami
KAFKA_INTER_BROKER_USER=user
BITNAMI_PKG_CHMOD=-R g+rwX
HOSTNAME=kafka-server2
KAFKA_VOLUMEDIR=/bitnami/kafka
KAFKA_BASEDIR=/opt/bitnami/kafka
KAFKA_LOG4J_OPTS=-Dkafka.logs.dir=/opt/bitnami/kafka/bin/../logs -Dlog4j.configuration=file:/opt/bitnami/kafka/bin/../config/log4j.properties
KAFKA_DATADIR=/bitnami/kafka/data
KAFKA_BROKER_USER=user
KAFKA_DAEMON_GROUP=kafka
PWD=/
HOME=/
KAFKA_DAEMON_USER=kafka
OS_NAME=linux
KAFKA_CONF_FILE=/opt/bitnami/kafka/conf/server.properties
KAFKA_ZOOKEEPER_PASSWORD=
KAFKA_LOGDIR=/opt/bitnami/kafka/logs
OS_ARCH=amd64
BITNAMI_APP_NAME=kafka
KAFKA_ZOOKEEPER_USER=
KAFKA_CFG_ZOOKEEPER_CONNECT=zookeeper-server:2181
ALLOW_PLAINTEXT_LISTENER=yes
BITNAMI_IMAGE_VERSION=2.4.0-debian-9-r20
KAFKA_PORT_NUMBER=9092
SHLVL=0
KAFKA_CONFDIR=/opt/bitnami/kafka/conf
KAFKA_BROKER_PASSWORD=bitnami
PATH=/opt/bitnami/kafka/bin:/opt/bitnami/kafka/bin:/opt/bitnami/java/bin:/opt/bitnami/kafka/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
KAFKA_HEAP_OPTS=-Xmx1024m -Xms1024m
KAFKA_CONF_FILE_NAME=server.properties
```

### Demo Notes (Nokia Containers)

Zookeeper cmd:

```bash
/etc/alternatives/jre_openjdk//bin/java -Xmx512M -Xms512M -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true -Xlog:gc*:file=/var/log/kafka/zookeeper-gc.log:time,tags:filecount=10,filesize=102400 -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=/var/log/kafka -Dlog4j.configuration=file:/etc/kafka/log4j.properties -cp /usr/bin/../share/java/kafka/*:/usr/bin/../support-metrics-client/build/dependant-libs-2.12/*:/usr/bin/../support-metrics-client/build/libs/*:/usr/share/java/support-metrics-client/* org.apache.zookeeper.server.quorum.QuorumPeerMain /etc/kafka/zookeeper.properties
```

Zookeeper Environment

```bash
HOSTNAME=9a4282575235
KAFKA_HEAP_OPTS=-Xmx512M -Xms512M
SCALA_VERSION=2.12
KAFKA_JMX_OPTS=-Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false  -Dcom.sun.management.jmxremote.ssl=false
ZOOKEEPER_CLIENT_PORT=2181
PYTHON_VERSION=2.7.5
KAFKA_OPTS=
KAFKA_LOG4J_OPTS=-Dkafka.logs.dir=/var/log/kafka -Dlog4j.configuration=file:/etc/kafka/log4j.properties
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
KAFKA_VERSION=5.3.1
PWD=/
JAVA_HOME=/etc/alternatives/jre_openjdk/
ZOOKEEPER_TICK_TIME=2000
LANG=en_US.UTF-8
ZOOKEEPER_SYNC_LIMIT=2
SHLVL=0
HOME=/root
COMPONENT=zookeeper
KAFKA_JMX_HOSTNAME=172.20.0.2
IS_RESTORE=false
```

Kafka Command Line:

```bash
/etc/alternatives/jre_openjdk//bin/java -Xmx1G -Xms1G -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent -Djava.awt.headless=true -Xlog:gc*:file=/var/log/kafka/kafkaServer-gc.log:time,tags:filecount=10,filesize=102400 -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false -Dcom.sun.management.jmxremote.ssl=false -Dkafka.logs.dir=/var/log/kafka -Dlog4j.configuration=file:/etc/kafka/log4j.properties -cp /usr/bin/../share/java/kafka/*:/usr/bin/../support-metrics-client/build/dependant-libs-2.12/*:/usr/bin/../support-metrics-client/build/libs/*:/usr/share/java/support-metrics-client/* kafka.Kafka /etc/kafka/kafka.properties
```

Kafka Environment. `$KAFKA_ADVERTISED_LISTENERS` and `$KAFKA_JMX_HOSTNAME` are dependent on which instance this is.
```
HOSTNAME=1cc99b2936b3
KAFKA_HEAP_OPTS=-Xmx1G -Xms1G
KAFKA_ZOOKEEPER_CONNECT=zookeeper-server:2181
SCALA_VERSION=2.12
KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://kafka-broker-3:9092
KAFKA_JMX_OPTS=-Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false  -Dcom.sun.management.jmxremote.ssl=false
PYTHON_VERSION=2.7.5
KAFKA_LOG4J_OPTS=-Dkafka.logs.dir=/var/log/kafka -Dlog4j.configuration=file:/etc/kafka/log4j.properties
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
KAFKA_VERSION=5.3.1
PWD=/
JAVA_HOME=/etc/alternatives/jre_openjdk/
LANG=en_US.UTF-8
SHLVL=0
HOME=/root
EXTERNAL_SECURITY_PROTOCOL=PLAINTEXT
INTERNAL_SECURITY_PROTOCOL=PLAINTEXT
COMPONENT=kafka
KAFKA_JMX_HOSTNAME=172.20.0.5
IS_RESTORE=false
```

For the Nokia Zookeeper and Kafka containers:

* `/usr/share/java/kafka/` are identical.
* `/etc/kafka/log4j.properties` are identical.
* `/usr/support-metrics-client/` does not exist.
* `/usr/share/java/support-metrics-client` does not exist.

Experimental:

```bash
DIR=/home/muth/tmp
TOP=../../../
KM_BIN=${TOP}/build/km/km
DYNLD=${TOP}/build/runtime/libc.so
JDK_KM=./build/linux-x86_64-server-release/jdk.km
   ${KM_BIN} --dynlinker=${DYNLD} --putenv="LD_LIBRARY_PATH=/opt/kontain/lib64:/lib64:${JDK_KM}/lib/" \
   ./build/linux-x86_64-server-release/jdk/bin/java.kmd \
   -Xmx512M -Xms512M -server -XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:InitiatingHeapOccupancyPercent=35 -XX:+ExplicitGCInvokesConcurrent \
   -Djava.awt.headless=true -Xlog:gc*:file=${DIR}/zookeeper-gc.log:time,tags:filecount=10,filesize=102400 \
   -Dcom.sun.management.jmxremote=true -Dcom.sun.management.jmxremote.authenticate=false -Dcom.sun.management.jmxremote.ssl=false \
   -Dkafka.logs.dir=${DIR}/zookeeper.log -Dlog4j.configuration=file:${DIR}/kafka_log4j.properties \
   -cp '${DIR}/kafka_jars/*' org.apache.zookeeper.server.quorum.QuorumPeerMain ${DIR}/etc_kafka/zookeeper.properties
```

Note: `-Djava.compiler=NONE` turns off JIT. Good for debugging.

## KM Image Notes

The Nokia Kafka and Zookeeper images have identical contents in `usr/share/java/kafka` (all `.jar` files) and `/etc/kafka` (configuration files), so only one copy is needed for KM.

In one window run:

```bash
docker run -it --rm kontainstage.azurecr.io/nokia/ckaf/kafka:2.0.0-5.3.1-2696 bash
```

In another window run (from $KM_BASE):

```bash
mkdir build/demo/nokia/km_kafka_image
docker cp <container_id>:/usr/share/java/kafka demo/nokia/km_kafka_image/usr_share_java_kafka
docker cp <container_id>:/etc/kafka build/demo/nokia/km_kafka_image/etc_kafka
```

When this is done, exit the docker container in the first window.

Create writable directories for Zookeeper and Kafka

```bash
mkdir build/demo/nokia/disks/zookeeper
mkdir build/demo/nokia/disks/kafka-broker-1
mkdir build/demo/nokia/disks/kafka-broker-2
mkdir build/demo/nokia/disks/kafka-broker-3
```

to try:

```bash
cd demo/nokia/kafka-test-tool/bin
bash -x createKMTestEnv.sh 1
```

Logs in `build/demo/nokia/disks`.
