ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontain/buildenv-km-${DTYPE}:${BUILDENV_IMAGE_VERSION}

# turn off km symlink trick and minimal shell interpretation
ENV KM_DO_SHELL NO
ADD --chown=0:0 busybox/_install run-test.sh run-all-tests.sh ./
ADD km .