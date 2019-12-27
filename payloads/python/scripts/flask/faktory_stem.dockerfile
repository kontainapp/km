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

# Prep all the files needed in Kontainer , in $TMP dir
COPY faktory_prepare.sh /tmp
RUN /tmp/faktory_prepare.sh /tmp/faktory

# This stage will unpack all py files from prior stage and add KM/python.km needed stuff
FROM scratch

COPY --from=original /tmp/faktory/pydirs/ /
COPY --from=original /tmp/faktory/python3 /usr/local/bin/
# TODO: build correct python3.km depending on payload;  package in tar as /usr/local/bin/python and use 'ADD'
COPY python3.km km /usr/local/bin/

# ENTRYPOINT and CMD will be added to to the end of this file by caller from upstairs
