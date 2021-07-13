# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


FROM scratch

# turn off km symlink trick and minimal shell interpretation
ENV KM_DO_SHELL NO
ADD --chown=0:0 busybox/_install /
# stopgap - krun will insert this at the beginning, put it here for now
ENTRYPOINT [ "/opt/kontain/bin/km" ]
