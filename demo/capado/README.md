# Copado Notes

Copado is a company that asked us to run benchmarks against two Java test applications, one that uses the Spring Boot framework (https://github.com/CopadoSolutions/springboot-poc.git) and the other that uses the Micronaut framework (https://github.com/CopadoSolutions/micronaut-poc.git).

The test applications rely on `postgres` and `redis` which were run using standard Docker containers using this proceudre:

1. Pull `postgres` and `redis` from Docker Hub.
2. Start `postgres`. `docker run --name my-postgres -e POSTGRES_PASSWORD=nopass -p 5432:5432 -d postgres`.
3. Start `redis`. `docker run --name my-redis -p 6379:6379 -d redis`.

# Building
## Building SpringBoot Application

Steps to build the Springboot Application
```
$ git clone https://github.com/CopadoSolutions/springboot-poc.git
$ cd springboot-poc
$ ./gradlew bootJar
```

The deployment JAR is `build/libs/springboot-poc-0.0.1-SNAPSHOT.jar`.

I currently copy the deployment JAR to `demo/capado` so `kontain.dockerfile` can find it and run `docker build -t kontainapp/capado-demo -f kontain.dockerfile .` to build the test container.

## Build Micronaut Application
```
$ git clone https://github.com/CopadoSolutions/micronaut-poc.git
$ cd micronaut-poc
$ ./gradlew assemble
```

The deployment JAR is `build/libs/micronaut-poc-0.1-all.jar`.


I currently copy the deployment JAR to `demo/capado` so `kontain_nm.dockerfile` can find it and run `docker build -t kontainapp/capado-demo-mn -f kontain_mn.dockerfile .` to build the test container.

# Running

Both applications use the same environment variables to configure the `postgres` and `redis` services they use.
The common values are:
```
set -x
export POSTGRE_USER=postgres
export POSTGRE_PWD=nopass
export REDIS_URL=redis://localhost:6379
```

There is a some variations between the Spring Boot and Micronaut `postgres` connectors. 
1. The Spring Boot connector is happy using `localhost`, the Micronaut connector insists on a IP address.
2. The micronaut connector requires that a database called `micronaut` exists before the application is run. Use `psql` on the postgres container to create it.

## Springboot

Postgres location:

```
export POSTGRE_URL=jdbc:postgresql://localhost:5432
```

## Micronaut
Micronaut requires it's postgres database to be created out of band before running.
Micronaut's postgres connector doesn't like localhost and prefers an IP address.

```
export POSTGRE_URL=jdbc:postgresql://<ipaddr>:5432/micronaut
```
