ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-dynamic-base-large:${RUNENV_IMAGE_VERSION}
COPY . /
