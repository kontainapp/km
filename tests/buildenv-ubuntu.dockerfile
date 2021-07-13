#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Dockerfile for builds, based on Ubuntu instead of Fedora
# Usage will be 'docker run <container> make TARGET=<target> - see ../../Makefile

FROM ubuntu

LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"

ARG USER=appuser
# Dedicated arbitrary in-image uid/gid, mainly for file access
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
