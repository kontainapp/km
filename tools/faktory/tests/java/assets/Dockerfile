FROM openjdk:13-jdk-alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM openjdk:13-jdk-alpine AS unpacker
WORKDIR /app
ARG APPJAR=/app/target/*.jar
COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java","-jar", "app.jar"]