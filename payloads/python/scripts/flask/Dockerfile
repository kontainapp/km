# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Python flask app example. Slightly modified to support different distros.
#

# distro (as well as add_pip3,below) MUST be set when building
# examples of distro selection:
#  --build-arg distro=alpine --build-arg add_pip3="apk add py3-pip"
#  --build-arg distro=ubuntu --build-arg add_pip3="apt-get update -y && apt-get install -y python3-pip"
#  --build-arg distro=fedora --build-arg add_pip3="dnf -y update && dnf -y install python3-pip"
# default:
#  alpine
#
ARG distro
ARG tag=latest

FROM $distro:$tag


# ideally we should set add_pip3 and update_index here depending on $distro, but Dockerfile has no if/then/ese so relying on the caller
ARG add_pip3
ARG update_index=true
LABEL maintainer="faktory@kontain.app"

RUN $update_index && $add_pip3 && pip3 install --upgrade pip
COPY . /app
WORKDIR /app

# installing to user to check multiple site-modules locations
RUN pip3 install --user --no-warn-script-location -r requirements.txt

ENTRYPOINT ["python3", "-B", "-E", "-OO", "-t"]
CMD ["flask_hello.py"]

