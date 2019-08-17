# Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile for builds, based on Ubuntu instead of Fedora
# Usage will be 'docker run <container> make TARGET=<target> - see ../../Makefile

FROM ubuntu

LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"

# uid=1001(vsts) gid=117(docker) groups=117(docker)
ARG USER=appuser
ARG UID=1001
ARG GID=117

RUN apt-get update; apt-get upgrade -y; \
   apt-get install -y make git gdb libelf-dev gcc-8 g++-8 libc-dev curl vim time python; \
   apt-get clean; \
   ln -s `which gcc-8` /bin/cc; \
   ln -s `which gcc-8` /bin/gcc; \
   ln -s `which g++-8` /bin/g++

RUN groupadd -f -g $GID appuser && useradd -r -u $UID -g $GID appuser
USER appuser
ENV TERM=xterm
