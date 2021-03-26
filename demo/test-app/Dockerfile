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
# Dockerfile for tensor flow inferencing test app

FROM kontain/test-python-fedora
USER root
RUN dnf install -y python3.8

WORKDIR /app
RUN echo -e '#!/bin/bash\ndate +%s%N >& /tmp/start_time; /usr/bin/python3.8.orig /app/app.py' > /app/run.sh && \
    echo -e '#!/bin/bash\ndate +%s%N >& /tmp/start_time; /opt/kontain/bin/km --mgtpipe /tmp/km.sock /usr/bin/python3.8.km /app/app.py' > /app/run_km.sh && \
    echo -e '#!/bin/bash\ndate +%s%N >& /tmp/start_time; /opt/kontain/bin/km kmsnap' > /app/run_snap.sh && \
    chmod +x /app/run*.sh
COPY app.py switch.sh requirements.txt tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl /app/

RUN pip install -t /usr/lib/python3.8/site-packages pip && \
	python3.8 -m pip install setuptools && \
	python3.8 -m pip install -r requirements.txt
RUN python3.8 -m pip install tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl
RUN cp /usr/lib64/libcrypto.so.1.1.1[a-z] /opt/kontain/alpine-lib/lib/libcrypto.so.1.1

# switch to 3.8
RUN ln -sf /usr/bin/python3.8 /usr/bin/python3

RUN cp ${PHOME}/python.kmd /usr/bin/python3.8.km && \
   mkdir ${PREFIX}/bin && cp ${PHOME}/km ${PREFIX}/bin/ && \
   mkdir ${PREFIX}/runtime/ && cp ${PHOME}/libc.so ${PREFIX}/runtime/ && \
   ln -sf ${PREFIX}/runtime/libc.so ${PREFIX}/alpine-lib/lib/ld-linux-x86-64.so.2
RUN mv /usr/bin/python3.8 /usr/bin/python3.8.orig
RUN ln -sf ${PREFIX}/bin/km /usr/bin/python3.8

CMD ["python", "app.py"]
# USER appuser
