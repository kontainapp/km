# Copyright © 2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Create image with pre-built java libs, so we can use it in fast linking of java.km

ARG MODE=Release
ARG JDK_VERSION=jdk-11.0.6+10
ARG BUILDENV_IMAGE_VERSION=latest

# Intermediate image with Java source, where Java is built.
# Thrown away after the the build results are taken from it
FROM kontain/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION} AS build-jdk
ARG JDK_VERSION
# ENV JDK_VERSION=$JDK_VERSION

# clone first, to save the layer if we need to modify further steps
RUN git config --global advice.detachedHead false
RUN git clone https://github.com/openjdk/jdk11u.git ${JDK_VERSION} -b ${JDK_VERSION}

USER root
RUN dnf install -y \
   java-11-openjdk java-11-openjdk-devel \
   autoconf zip unzip fontconfig-devel cups-devel  alsa-lib-devel \
   libXtst-devel libXt-devel libXrender-devel libXrandr-devel libXi-devel
USER $USER

ADD jtreg-4.2-b16.tar.gz /home/$USER/
RUN cd ${JDK_VERSION} && bash configure \
   --disable-warnings-as-errors --with-native-debug-symbols=internal \
   --with-jvm-variants=server --with-zlib=bundled --with-jtreg=/home/$USER/jtreg \
   --enable-jtreg-failure-handler
RUN make -C ${JDK_VERSION} images
ARG BUILD=/home/$USER/jdk-11.0.6+10/build/linux-x86_64-normal-server-release/

RUN find ${BUILD} -name '*.so' | xargs strip
RUN rm ${BUILD}/images/jdk/lib/src.zip

# Build the target image
FROM kontain/buildenv-km-fedora:${BUILDENV_IMAGE_VERSION}

ARG BUILD=/home/$USER/jdk-11.0.6+10/build/linux-x86_64-normal-server-release/
ENV JAVATOP=/home/$USER/java

RUN mkdir -p ${JAVATOP}
WORKDIR ${JAVATOP}
COPY --from=build-jdk --chown=appuser:appuser ${BUILD}/support/native/java.base/java/ support/native/java.base/java/
COPY --from=build-jdk --chown=appuser:appuser ${BUILD}/images/jdk/lib images/jdk/lib
# This will allow to compile .java files with correct java version
COPY --from=build-jdk --chown=appuser:appuser ${BUILD}/images/jdk/bin/javac images/jdk/bin/javac

# TODO: check if we can build JRE instead of JDK to make less junk
