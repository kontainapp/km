ARG runenv_image_version=latest

FROM kontain/runenv-busybox:${runenv_image_version}
COPY node /usr/local/bin/
