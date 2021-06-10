ARG runenv_image_version=latest

FROM kontainapp/runenv-busybox:${runenv_image_version}
COPY . /
ENTRYPOINT [ "/opt/kontain/bin/km", "/usr/local/bin/python3" ]
