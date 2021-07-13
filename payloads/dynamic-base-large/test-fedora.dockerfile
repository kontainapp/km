# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


ARG DTYPE=fedora
ARG BUILDENV_IMAGE_VERSION=latest

FROM kontainapp/buildenv-dynamic-base-${DTYPE}:${BUILDENV_IMAGE_VERSION}
ADD libc.so /opt/kontain/runtime/
ADD km hello_test.kmd ./
