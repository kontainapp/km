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
# Dockerfile for build image. There are three stages:
#
# buildenv-base - basic from OS image with the necessary packages installed
# build-libstdcpp - clone gcc source, then build and install libstdc++
# buildenv - based on buildenv-base and just copy the installed part from the previous
#
# Usage will be 'docker run <container> make TARGET=<target> - see ../../Makefile

FROM fedora:31 AS buildenv-base
ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
ARG UID=1001
ARG GID=117

RUN groupadd -f -g $GID $USER && useradd -r -u $UID -g $GID $USER && mkdir /home/$USER && chown $UID.$GID /home/$USER

ENV TERM=xterm
ENV USER=$USER
ENV PREFIX=/opt/kontain
WORKDIR /home/$USER

# some of the packages needed only for payloads and /or faktory, but we land them here for convenience
# Also, this list is used on generating local build environment, so we explicitly add
# some packages which are alwyas present on Fedora31 but may be missing on Fedora30 (e.g jinja2)
RUN dnf install -y \
   gcc gcc-c++ make gdb git-core gcovr \
   time patch file findutils diffutils which procps-ng python2 \
   glibc-devel glibc-static libstdc++-static \
   elfutils-libelf-devel elfutils-libelf-devel-static bzip2-devel \
   zlib-static bzip2-static xz-static \
   openssl-devel openssl-static \
   expat-static jq googler python3-jinja2 \
   && dnf upgrade -y && dnf clean all && rm -rf /var/cache/dnf

FROM buildenv-base AS buildenv
LABEL version="1.1" maintainer="Mark Sterin <msterin@kontain.app>"

USER root
# Prep both alpine-lib and kontain runtime dirs
RUN for i in runtime alpine-lib ; do \
   mkdir -p $PREFIX/$i && chgrp users $PREFIX/$i && chmod 777 $PREFIX/$i ; \
   done

USER $USER
COPY alpine-lib $PREFIX/