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
# Dockerfile for tests.
# Usage will be 'docker run -it --rm --env BRANCH=$(git rev-parse --abbrev-ref HEAD) --device=/dev/kvm --user 0 test-bats km_cdocker run <container> make TARGET=<target> - see ../../Makefile
#

FROM km-buildenv-fedora:latest

ENV BATS time bats/bin/bats
ENV TEST_FILES km_core_tests.bats
ENV TIME_INFO /tests/time_info.txt
ENV KM_BIN /tests/km
COPY . /tests
WORKDIR /tests
ENV PATH=/tests/bats/bin:.:$PATH

#ENTRYPOINT ["/usr/bin/bash", "/tests/bats/bin/bats"]



