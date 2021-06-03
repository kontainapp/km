ARG runenv_image_version=latest

FROM kontain/runenv-node:${runenv_image_version}

COPY scripts /scripts/
COPY docker-entrypoint.sh .
EXPOSE 8080
CMD [ "docker-entrypoint.sh", "node", "/scripts/micro-srv.js", "8080" ]