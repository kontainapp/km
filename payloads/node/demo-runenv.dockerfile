ARG runenv_image_version=latest

FROM kontain/runenv-node:${runenv_image_version}

COPY scripts /scripts
EXPOSE 8080
CMD [ "/scripts/micro-srv.js", "8080" ]