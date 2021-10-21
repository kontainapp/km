# Container to test containerd in
# Run this privileged.
FROM fedora:33

RUN dnf update -y

RUN dnf install -y file procps strace pigz yajl libseccomp

COPY bin/* /usr/local/bin/
COPY config.toml /etc/containerd/config.toml
