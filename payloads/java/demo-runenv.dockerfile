ARG runenv_image_version=latest

FROM kontain/runenv-jdk-11.0.8:${runenv_image_version}
COPY scripts /scripts
EXPOSE 8080
ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "/opt/kontain/java/bin/java.kmd" ]
CMD ["-cp", "/scripts", "SimpleHttpServer"]