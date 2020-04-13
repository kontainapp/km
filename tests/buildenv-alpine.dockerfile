# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile for build base image for alpine-based builds, both for .km files and Linux native executables.
#

FROM alpine:3.11.5

LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"
ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
ARG UID=1001
ARG GID=117
RUN apk update && apk upgrade && apk add --no-cache \
   alpine-sdk sudo gdb elfutils-libelf zlib-dev libffi-dev git gcovr bash libc-dev linux-headers \
   elfutils-dev ncurses coreutils shadow curl go
RUN sed -i -e 's/# \(%wheel.*NOPASSWD\)/\1/' /etc/sudoers &&  groupadd -f -g $GID $USER && \
   useradd -r -m -u $UID -g $GID $USER -G wheel && \
   mkdir -p /opt/kontain/runtime; chmod -R 777 /opt/kontain
USER $USER

ENV TERM=xterm
ENV USER=$USER
WORKDIR /home/$USER
ENTRYPOINT ["/bin/bash"]
