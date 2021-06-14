ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}
ADD runtime /opt/kontain/runtime/
