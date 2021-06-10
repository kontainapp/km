ARG runenv_image_version=latest

FROM kontainapp/runenv-busybox:${runenv_image_version}

COPY dweb /dweb
WORKDIR /dweb