# Install KM

TBD

# Install Faktory

TBD

# Java

## Download kontain Java runtime environment
Download the kontain JDK image:
```
docker pull kontainapp/runenv-jdk-11:latest
```

## Using faktory to convert an existing java based image

To convert an existing image `example/existing-java:latest` into a kontain
based image `example/kontain-java:latest`:
```bash
# sudo may be required since faktory needs to look at files owned by dockerd
# and containerd, which is owned by root under `/var/lib/docker`
sudo faktory convert \
    example/existing-java:latest \
    example/kontain-java:latest \
    kontainapp/runenv-jdk-11:latest \
    --type java
```

## Use kontain Java in dockerfiles

To use kontain java runtime environment with dockerfile, user can substitude
the base image with kontain image.
```dockerfile
FROM kontainapp/runenv-jdk-11

# rest of dockerfile remain the same ...
```

Here is an example dockerfile without kontain, to build and package the
`springboot` starter `gs-rest-service` repo from `spring-guides` found
[here](https://github.com/spring-guides/gs-rest-service.git). We use
`adoptopenjdk/openjdk11:alpine` and `adoptopenjdk/openjdk11:alpine-jre` as
base image as an example, but any java base image would work.

```dockerfile
FROM adoptopenjdk/openjdk11:alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM adoptopenjdk/openjdk11:alpine-jre
WORKDIR /app
ARG APPJAR=/app/target/*.jar
COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java","-jar", "app.jar"]
```

To package the same container using kontain:
```dockerfile
FROM adoptopenjdk/openjdk11:alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM kontainapp/runenv-jdk-11
WORKDIR /app
ARG APPJAR=/app/target/*.jar
COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java","-jar", "app.jar"]
``` 
Note: only the `FROM` from the final docker image is changed. Here we kept
using a normal jdk docker image as the `builder` because the build
environment is not affected by kontain.

## Run

To run a kontain based container image, the container will need access to
`/dev/kvm` and kontain monitor `km`. For Java we also requires kontain's
version of `lic.so`.

```bash
docker run -it --rm \
    --device /dev/kvm \
    -v /opt/kontain/bin/km:/opt/kontain/bin/km:z \
    -v /opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z \
    example/kontain-java
```
