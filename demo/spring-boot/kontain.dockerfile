FROM kontain/runenv-jdk-11.0.8:latest
RUN apk add --update coreutils
RUN echo 'date +%s%N >& /tmp/start_time; /opt/kontain/bin/km --mgtpipe /tmp/km.sock /opt/kontain/java/bin/java.kmd -XX:-UseCompressedOops -jar /app.jar' > /run.sh
RUN echo 'date +%s%N >& /tmp/start_time; /opt/kontain/bin/km --resume kmsnap' > /run_snap.sh
COPY test/test.sh /test.sh
ARG TARGET_JAR_PATH
COPY ${TARGET_JAR_PATH} /app.jar
EXPOSE 8080/tcp
ENTRYPOINT ["/opt/kontain/bin/km", "/opt/kontain/java/bin/java.kmd", "-XX:-UseCompressedOops", "-jar", "/app.jar"]