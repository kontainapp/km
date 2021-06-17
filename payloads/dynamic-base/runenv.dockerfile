ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}
ADD --chown=0:0 runtime /opt/kontain/runtime/
ADD --chown=0:0 lib /opt/kontain/lib/
