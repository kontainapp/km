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

This will create docker image called `kontainapp/spring-boot-demo`.

### Step 2 - Start a Shell Inside Docker Container

```bash
docker run --name=KM_SpringBoot_Demo --privileged --rm -it --device=/dev/kvm \
 -v /opt/kontain/bin/km:/opt/kontain/bin/km:z \
 -v /opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z \
 -v /opt/kontain/bin/km_cli:/opt/kontain/bin/km_cli:z \
 -v ${WORKSPACE}/km/payloads/java/scripts:/scripts:z \
 --entrypoint /bin/sh -p8080:8080 kontainapp/spring-boot-demo
```

### Step 3 Measure Base Startup Time

Outside of the container run the program `demo/spring-boot/test/test.sh`.
This program will get the time when the server first responds.
Then it will get the server start time from the container and print the difference.

Inside the container run

```sh
sh /run.sh
```

This prints the time in nanoseconds when we start the server in `/tmp/start_time` file in the container.
The difference between the two values is the time it took for the server to respond to requests will be printed by test.sh command `Response time 7.432 secs`.

### Step 4 Take a Snapshot

```bash
docker exec KM_SpringBoot_Demo /opt/kontain/bin/km_cli -s /tmp/km.sock
```

### Step 5 - Measure Snapshot Startup Time

Outside of the container run the program `demo/spring-boot/test/test.sh`.
As before, this program will get the time when the server first responds.
Then it will get the server start time from the container and print the difference.

Inside the container run

```sh
sh /run_snap.sh
```

Same as before, the time difference between the first response and the server start will be printed as `Response time 1.432 secs`

## References:

`https://spring.io/quickstart`
`https://spring.io/guides/gs/rest-service/`
`https://spring.io/guides/topicals/spring-boot-docker/`
`https://medium.com/@sandhya.sandy/spring-boot-microservices-building-a-microservices-application-using-spring-boot-d9cbb96b9ed4`

## Capado Notes

Capado provided us with a simple Java application the requires a `postgres` database and a `redis`
service. They provided two variations of the application, the first built with Spring Boot and the
second built with Micronaut.

Spring Boot: `https://github.com/CopadoSolutions/springboot-poc`

`./gradlew bootRun` to test locally.

`./gradlew bootJar` to build a self-contained jar file.
Launch jar file with `java -jar build/libs/springboot-poc-0.0.1-SNAPSHOT.jar`.

Micronaut: `https://github.com/CopadoSolutions/micronaut-poc`

## For Both

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
FROM kontain/runenv-jdk-11.0.8:latest
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
FROM kontain/runenv-jdk-11.0.8:latest
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
