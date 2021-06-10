ARG runenv_image_version=latest
FROM kontainapp/runenv-busybox:${runenv_image_version}
CMD ["/bin/sh", "-c", "ls -l && echo Hello"]