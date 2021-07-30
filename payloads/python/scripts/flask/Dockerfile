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

ENTRYPOINT ["python3", "-B", "-OO", "-t"]
CMD ["flask_hello.py"]

