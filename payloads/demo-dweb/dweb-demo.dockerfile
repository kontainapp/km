# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


# We'll use this just to show size/start time for dweb in a docker container
FROM ubuntu

ADD dweb/dweb dweb/index.html dweb/kontain_logo_transparent.png dweb/text.txt /dweb/
ADD dweb/fonts /dweb/fonts/
ADD dweb/js /dweb/js/
ADD dweb/css /dweb/css/
WORKDIR /dweb
