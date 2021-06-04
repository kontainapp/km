ARG runenv_image_version=latest
FROM kontain/runenv-busybox:${runenv_image_version}
CMD ["/bin/sh", "-c", "ls -l"]