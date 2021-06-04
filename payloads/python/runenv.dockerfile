ARG runenv_image_version=latest

FROM kontain/runenv-busybox:${runenv_image_version}
COPY . /
ENTRYPOINT [ "/opt/kontain/bin/km", "/usr/local/bin/python3" ]
