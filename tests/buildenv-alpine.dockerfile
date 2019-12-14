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
# Dockerfile for builds.
# Usage will be 'docker run <container> make TARGET=<target> - see ../../Makefile

FROM alpine

LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"

ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
ARG UID=1001
ARG GID=117

RUN apk add --no-cache \
   build-base  elfutils-libelf zlib-dev gdb git gcovr bash \
   libc-dev linux-headers elfutils-dev \
   ncurses coreutils shadow curl

RUN groupadd -f -g $GID appuser && useradd -r -u $UID -g $GID appuser
USER appuser
ENV TERM=xterm
