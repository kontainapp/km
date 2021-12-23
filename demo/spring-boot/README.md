# Spring-Boot Demo

Spring-Boot is a very popular framework in the Java world. It automates linkage and
dependency issues that are very difficult and error-prone to perform by hand.
Some of this automation occurs at runtime, typically during program initialization.
Hence, Spring-Boot applications have a reputation for being slow to start.

KM snapshots can help mitigate spring-boot startup times by starting a image of a process after it has been initialized. Time saves the bulk of initialization time.

## How to Measure Spring-Boot Startup Performance Improvement.

Note: We use a docker container to allow our java to find everything.

### Step 1 - Build Everything

```bash
make -j
make -C payloads/java
make -C payloads/java runenv-image
make -C demo/spring-boot
```

This will create docker image called `kontainapp/spring-boot-demo` and make sure km_cli (needed for initiating a snapshot)
is available in /opt/kontain/bin.

### Step 2 - Start a Shell Inside Docker Container

```bash
docker run --runtime=krun --name=KM_SpringBoot_Demo --rm -it \
  -v /opt/kontain/bin/km_cli:/opt/kontain/bin/km_cli \
  -v $(pwd):/mnt:rw -p8080:8080 kontainapp/spring-boot-demo /bin/sh
```

### Step 3 Measure Base Startup Time

Outside of the container run `test.sh`. If in the source tree, it is available here:

```sh
cd demo/spring-boot; ./test/test.sh
```

This program will get the time when the server first responds.
Then it will get the server start time from the container (started in the next step) and print the difference.

Inside the container run

```sh
/run.sh
```

This prints the time in nanoseconds when we start the server in `/tmp/start_time` file in the container.
The difference between the two values is the time it took for the server to respond to requests will be printed by test.sh command `Response time 7.432 secs`.

### Step 4 Take a Snapshot

```bash
docker exec KM_SpringBoot_Demo /opt/kontain/bin/km_cli -s /tmp/km.sock
```

This will generate a `kmsnap` file with re-usable snapshot.
Note that KM will report it as saving a coredump, since coredumps and snapshots have the same format
and are generated using the same mechanism.

### Step 5 - Measure Snapshot Startup Time

Outside of the container run the program `demo/spring-boot/test/test.sh`.
As before, this program will get the time when the server first responds.
Then it will get the server start time from the container and print the difference.

Inside the container run

```sh
/run_snap.sh
```

Same as before, the time difference between the first response and the server start will be printed as `Response time 1.432 secs`

## How to package snapshot as a kontainer

### Step 1 - Build Everything

Same as in the demo

### Step 2 - Start the kontainer

```bash
docker run --runtime=krun --name=KM_SpringBoot_Demo \
  -v /opt/kontain/bin/km_cli:/opt/kontain/bin/km_cli \
  -v $(pwd):/mnt:rw -p8080:8080 kontainapp/spring-boot-demo
```

### Step 3 - Serve a request, take a snapshot, and make snap kontainer

Send a request using curl, one or a few times:

```bash
curl http://localhost:8080/greeting | jq .
```

Make a snapshot and copy it from the container:

```bash
docker exec KM_SpringBoot_Demo /opt/kontain/bin/km_cli -s /tmp/km.sock
docker cp KM_SpringBoot_Demo:kmsnap .
chmod +x kmsnap
docker build -t kontainapp/snap_spring-boot-demo -f snap_kontain.dockerfile .
```

### Step4 - Run the new kontainer

```bash
docker run --runtime=krun --rm -it --name=KM_Snap_SpringBoot_Demo -p8080:8080 kontainapp/snap_spring-boot-demo
```

Verify the response, confirm the id number continues from the initial run

```bash
curl http://localhost:8080/greeting | jq .
```

## References:

`https://spring.io/quickstart`
`https://spring.io/guides/gs/rest-service/`
`https://spring.io/guides/topicals/spring-boot-docker/`
`https://medium.com/@sandhya.sandy/spring-boot-microservices-building-a-microservices-application-using-spring-boot-d9cbb96b9ed4`

## Generic steps

1. Pull `postgres` and `redis` from Docker Hub.
2. Start `postgres`. `docker run --name my-postgres -e POSTGRES_PASSWORD=nopass -p 5432:5432 -d postgres`.
3. Start `redis`. `docker run --name my-redis -p 6379:6379 -d redis`.

## For Spring Boot

Use this environment:

```bash
export POSTGRE_URL=jdbc:postgresql://localhost:5432
export POSTGRE_USER=postgres
export POSTGRE_PWD=nopass
export REDIS_URL=redis://localhost:6379

```

`./gradlew bootRun` runs the application locally.

`./gradlew bootJar` builds a self-contained jar file which allows the suitable
for `java -jar build/libs/springboot-poc-0.0.1-SNAPSHOT.jar`

Docker file:

```Dockerfile
FROM kontainapp/runenv-jdk-11.0.8:latest
COPY springboot-poc-0.0.1-SNAPSHOT.jar springboot-poc-0.0.1-SNAPSHOT.jar
EXPOSE 9090/tcp
ENV POSTGRE_URL jdbc:postgresql://localhost:5432
ENV POSTGRE_USER postgres
ENV POSTGRE_PWD nopass
ENV REDIS_URL redis://localhost:6379
```

## Copado Micronaut

Note
: Unlike Sprint Boot, Micronaut's postgres connector requires the `micronaut`
database be created band before running. Use `psql` for this.

Note
: Micronaut's postgres connector doesn't like localhost. Configuration must
use an IP address.

The Micronaut environment looks like this:

```bash
set -x
export POSTGRE_URL=jdbc:postgresql://<ipaddr>:5432/micronaut
export POSTGRE_USER=postgres
export POSTGRE_PWD=nopass
export REDIS_URL=redis://localhost:6379
```

`./gradlew bootRun` runs the application locally.

`./gradlew assemble` build a self-contained jar file.
To run `java -jar build/libs/micronaut-poc-0.1-all.jar`

Dokerfile:

```Dockerfile
FROM kontainapp/runenv-jdk-11.0.8:latest
COPY micronaut-poc-0.1-all.jar micronaut-poc-0.1-all.jar
EXPOSE 9091/tcp

ENV POSTGRE_URL=jdbc:postgresql://<ip addr>:5432/micronaut
ENV POSTGRE_USER=postgres
ENV POSTGRE_PWD=nopass
ENV REDIS_URL=redis://localhost:6379
```

TODO
: Docker Compose (?)
~


## Vagrant notes

This demo can be fully run on Mac or any other platform using HashiCorp Vagrant.
Vagrant support is in TOP/tools/hashicorp/, and the readme.md file there has details on Vagrant usage for KM.

High level

* provision a vm with KM/KKM installed from ubuntu box, using `vagrant up --provision`, OR just grab pre-created box and bring up a VM using `vagrant init ; vagrant up`
* ssh to the vm (`vagrant ssh`) and follow the guidance above.