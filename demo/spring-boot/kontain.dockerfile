FROM kontainapp/runenv-jdk-11.0.8:latest
ARG TARGET_JAR_PATH
COPY ${TARGET_JAR_PATH} /app.jar
COPY run.sh run_snap.sh /
ADD empty_tmp /tmp/
EXPOSE 8080/tcp
CMD ["/opt/kontain/java/bin/java", "-XX:-UseCompressedOops", "-jar", "/app.jar"]