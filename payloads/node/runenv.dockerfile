ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}
COPY node /usr/local/bin/
