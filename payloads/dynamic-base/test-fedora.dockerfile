ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-dynamic-base-${DTYPE}:${BUILDENV_IMAGE_VERSION}

ADD libc.so /opt/kontain/runtime/
ADD km hello_test.kmd ./
