# Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile to package python.kmd and friends for testing in CI/CD

ARG DTYPE=fedora
ARG IMAGE_VERSION=latest

FROM kontainapp/test-python-${DTYPE}:${IMAGE_VERSION}
RUN mv python.kmd.mimalloc python.km
ADD libc.so /opt/kontain/runtime/
ADD libmimalloc.so* /opt/kontain/lib/