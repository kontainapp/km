ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-node:${RUNENV_IMAGE_VERSION}

COPY scripts /scripts/
COPY docker-entrypoint.sh .
EXPOSE 8080
CMD [ "docker-entrypoint.sh", "node", "/scripts/micro-srv.js", "8080" ]