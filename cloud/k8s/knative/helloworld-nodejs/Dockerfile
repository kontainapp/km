# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


FROM kontainapp/runenv-node
EXPOSE 8080
COPY km /opt/kontain/bin/km
COPY helloworld.js /opt/kontain/bin/helloworld.js
ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "/node.km", "/opt/kontain/bin/helloworld.js" ]
