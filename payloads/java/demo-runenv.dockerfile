ARG runenv_image_version=latest

FROM kontainapp/runenv-jdk-11.0.8:${runenv_image_version}
COPY scripts /scripts
EXPOSE 8080
CMD ["-cp", "/scripts", "SimpleHttpServer"]