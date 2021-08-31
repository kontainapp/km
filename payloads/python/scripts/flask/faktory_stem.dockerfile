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
# First example of Faktory - "convert regular container to Kontainer".
# Shows conversion of python flask example.
#
# NOTE: we should generally maintain the same versioning scheme as Python, i.e. 'python3.7.4.km' and symlinks to it from Python3.7 and Python3

# image to convert to Kontainer
ARG image=flask
# Original image's tag. In out tests,we put distro (e.g. alpine) there.
ARG distro=alpine

# This stage will find and extract all py-related files from original container into $TMP folder
FROM $image:$distro as original

# Prep all the files needed in Kontainer, in $TMP dir
COPY faktory_prepare.sh /tmp
RUN /tmp/faktory_prepare.sh /tmp/faktory 

# This stage will unpack all py files from prior stage and add KM/python.km needed stuff
FROM kontainapp/runenv-python

COPY --from=original /tmp/faktory/pydirs/ /

# ENTRYPOINT and CMD will be added to to the end of this file by caller from upstairs
