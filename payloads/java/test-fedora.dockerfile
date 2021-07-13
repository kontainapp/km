# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


FROM fedora:31

# Contains the scripts needs to run tests.
COPY scripts ./scripts
COPY km /opt/kontain/bin/km
COPY libc.so /opt/kontain/runtime/
