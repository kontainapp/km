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
# Dockerfile for build python image. There are two stages:
#
# buildenv-cpython - based on kontainapp/buildenv-km-fedora, git clone, compile and test node
# linkenv - based on km-build-env and just copy the objects and test files

ARG BUILDENV_IMAGE_VERSION=latest
FROM kontainapp/buildenv-python-fedora:${BUILDENV_IMAGE_VERSION}
