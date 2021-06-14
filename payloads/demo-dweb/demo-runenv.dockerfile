ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-dweb:${RUNENV_IMAGE_VERSION}

EXPOSE 8080
CMD [ "dweb", "8080" ]