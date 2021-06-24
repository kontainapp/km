ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}
ADD --chown=0:0 libs.tar /opt/kontain/
