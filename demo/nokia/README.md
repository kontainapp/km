# Nokia Demo Directory

This directory contains demo work for Nokia Software. The directories
are:

* kontain-faktory - build system for building Java Kontainers from Nokia Kafka and zookeeper images. See `kontain-faktory/README.md` for information on how to build the images. You `need` to build them before you can run Nokia Kafka perf tests against Kontainers  (i.e. Java  unikernel in Kontain VM)
* kafka-test-tool - Java based Kafka Tests. See `kafka-test-tools/README.md` for Nokia's instructions on this.
* test-app - Python machine learning. (Suspended work for now)

## Steps

### Login to Azure and pull Nokia images

`make -C kontain-faktory login pull`.
Note that this assumes you did configure non-interactive login (see `docs/azure_pipeline.md` for how-to).
If you did not set up non-interactive login, run the following:

```bash
make -C ~/workspace/km/cloud/azure/ login
az acr login -n kontainstage
make -C kontain-faktory pull
```

## Install necesary packages

* `sudo dnf install sysstat` is required for tests which collect `sar` files:
* `sudo dnf install buildah` is required for flattening Kontainers (flattening is optional, but it does shrink Kontainers by half)
* in case you want to visualize `sar` output, install `ksar` from [here](https://github.com/vlsi/ksar)

## Build java.km

```bash
make -C ~/workspace/km
make -C ~/workspace/km/payloads/java
```

Optionally, validate the java.km build:
`make -C ~/workspace/km/payloads/java runenv-image validate-runenv-image`

## Build the Kontainers

* `cd kontain-faktory; make` to build multilayer Kontainers, or
* `cd kontain-faktory; make flatten` to build build multilayer Kontainers and flatten then into half the size


## Run the test with Kontainers

```bash
cd kafka-test-tool/bin
export CPREFIX=kontain/nokia
./startTest.sh test-case-ckaf-01
```

## More info

* See `kontain-faktory/README.md` for information about automation to build and validate Kontainers, how to clean up storage for Kafka runs, as well as misc helper `make` targets
* See `kafka-test-tools/README.md` for Nokia's detailed instructions on running tests.

### Tests

There are two separate tests included under `kafka-test-tools/bin`: `test-case-01.sh` and `test-case-ckaf-01.sh`.

`test-case-01.sh` is a  standard test that uses `docker-compose` and the
`bitnami` images from `dockerhub`. **We do not use it**

```bash
cd demo/nokia/kafka-test-tool
./bin/startTest.sh test-case-01
```

`test-case-ckaf-01.sh` is Nokia's modifications and uses the docker images
from our private Azure container registry. **This is the one we use**

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