ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-jdk-11.0.8:${RUNENV_IMAGE_VERSION}
COPY scripts /scripts
EXPOSE 8080
CMD ["/opt/kontain/java/bin/java", "-cp", "/scripts", "SimpleHttpServer"]
