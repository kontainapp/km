# Intructions from Nokia

## Prerequisits

Please install following softwares
- Linux environment, ubuntu or centos
- Docker : https://docs.docker.com/install/linux/docker-ce/ubuntu/
- Sysstat : sudo apt-get install sysstat
- docker-compose (optional)


## Setup Docker

Perform post installation steps in following URL
https://docs.docker.com/install/linux/linux-postinstall/


## CSF Kafka Test Environment Setup

import images
```
docker load < ckaf-zookeeper-2.0.0-3.4.14-2696.tgz
docker load < ckaf-kafka-2.0.0-5.3.1-2696.tgz
```

edit with text editor startTest.sh under kafka-test-tool/bin and set the BASE_DIR environment to installation directory
Example:
BASE_DIR=/devel/kontain/kafka-test-tool

## Perform test

Fire the startTest.sh script with related test case, for example:
./bin/startTest.sh ./startTest.sh test-case-ckaf-01

The script startTest.sh performs following actions:
- create a directory under results/<datetime>
- the output of script is in results/<datetime>/<datetime>-output.log
- start sar, the sar is in results/<datetime>/<datetime>-sar.txt
- call test case script
- kill sar after the test case execution

typical test case is performing following steps:
- create environment, here 3 node cluster
- create topic for test, here 3 replica
- perform load to kafka using kafka-client, 50000000 message
- output results
- destroy the environment


Ksar is nice tool to visualize the sar output, it requires jdk installation
https://github.com/vlsi/ksar






