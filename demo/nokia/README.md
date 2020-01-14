# Nokia Demo Directory

This directory contains demo work for Nokia Software. The directories
are:

* test-app - Python machine learning. (Suspended work for now)
* kafka-test-tool - Java based Kafka Tests

## kafka-test-tools

See `kafka-test-tools/README.md` for Nokia's instructions on this.

The needed images are in an Azure private container registry.
To retreive them into you local docker run:
```
make -C cloud/azure/ login 
az acr login -n kontainstage
docker pull kontainstage.azurecr.io/nokia/ckaf/zookeeper:2.0.0-3.4.14-2696
docker pull kontainstage.azurecr.io/nokia/ckaf/kafka:2.0.0-5.3.1-2696
docker pull kontainstage.azurecr.io/nokia/atg/kafka-client:4.2.1
```

The `sysstat` package is also required:
```
sudo dnf install sysstat
```

### Tests
There are two separate tests included under `kafka-test-tools/bin`: `test-case-01.sh` and `test-case-ckaf-01.sh`. 

`test-case-01.sh` is a  standard test that uses `docker-compose` and the
`bitnami` images from `dockerhub`.

```
cd demo/nokia/kafka-test-tool
./bin/startTest.sh test-case-01
```

`test-case-ckaf-01.sh` is Nokia's modifications and uses the docker images
from our private Azure container registry.
```
cd demo/nokia/kafka-test-tool/bin
./startTest.sh test-case-ckaf-01
```

These tests leave their results in `build/demo/nokia/results`. Each test run creates a sub-directory that contains a log file (`*-output.log`) and performance data (`*-sar.txt`).

Nokia uses `ksar` to visualize the performance data. Download a jar file from `https://github.com/vlsi/ksar/releases`.
To run:
```
java -jar ~/Downloads/ksar-5.2.4-b325_gdea8d8b5-SNAPSHOT-all.jar
```