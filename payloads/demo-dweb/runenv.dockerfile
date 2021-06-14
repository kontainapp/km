ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}

COPY dweb /dweb
WORKDIR /dweb