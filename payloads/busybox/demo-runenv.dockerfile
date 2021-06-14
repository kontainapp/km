ARG RUNENV_IMAGE_VERSION=latest
FROM kontainapp/runenv-busybox:${RUNENV_IMAGE_VERSION}
CMD ["/bin/sh", "-c", "ls -l && echo Hello"]