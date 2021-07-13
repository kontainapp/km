# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-dynamic-python:${RUNENV_IMAGE_VERSION}

COPY scripts /scripts
EXPOSE 8080
CMD ["/usr/local/bin/python3", "/scripts/micro_srv.py", "8080"]
