FROM kontain/runenv-jdk-11.0.8:latest
ARG TARGET_JAR_PATH
COPY ${TARGET_JAR_PATH} /app.jar
EXPOSE 8080/tcp
ENTRYPOINT ["/opt/kontain/bin/km", "/opt/kontain/java/bin/java.kmd", "-XX:-UseCompressedOops", "-jar", "/app.jar"]